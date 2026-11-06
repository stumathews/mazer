

#include "LevelManager.h"

#include <EventNumber.h>
#include <GameData.h>
#include <events/NetworkPlayerJoinedEvent.h>
#include <events/SceneChangedEvent.h>
#include <events/StartNetworkLevelEvent.h>
#include <events/UpdateProcessesEvent.h>
#include <scene/SceneManager.h>
#include <ElapsedGameTimeProvider.h>
#include <SDL_mixer.h>
#include <common/Common.h>
#include <mazer/Level.h>
#include <mazer/PlayerCollidedWithEnemyEvent.h>
#include <mazer/PlayerCollidedWithPickupEvent.h>
#include <cppgamelib/events/GameObjectEvent.h>
#include <file/Logger.h>
#include <mazer/Player.h>
#include <mazer/GameObjectEventFactory.h>
#include <processes/DelayProcess.h>
#include <mazer/CharacterBuilder.h>
#include <mazer/Room.h>
#include <mazer/Rooms.h>
#include <cppgamelib/asset/SpriteAsset.h>
#include <cppgamelib/objects/GameObjectFactory.h>
#include <events/PlayerMovedEvent.h>
#include <mazer/GameObjectMoveStrategy.h>
#include <cppgamelib/character/AnimatedSprite.h>
#include <cppgamelib/character/Inventory.h>
#include <cppgamelib/common/constants.h>
#include <cppgamelib/objects/GameObject.h>
#include <mazer/pickup.h>

using namespace gamelib;
using namespace mazer;
using namespace std;

bool LevelManager::Initialize()
{
	if (initialized) { return true; }

	// Obtain game settings
	verbose = GetBoolSetting("global", "verbose");
	disableCharacters = GetBoolSetting("global", "disableCharacters");
	isGameServer = SettingsManager::Get()->GetBool("networking", "isGameServer");
	sendRateMs = SettingsManager::Get()->GetInt("gameStatePusher", "sendRateMs");
	const auto gameStatePusherEnabled = SettingsManager::Get()->GetBool("gameStatePusher", "enabled");

	// Set game data
	mazer::GameData::Get()->IsNetworkGame = GetBoolSetting("global", "isNetworkGame");
	mazer::GameData::Get()->IsGameDone = false;
	mazer::GameData::Get()->IsNetworkGame = false;
	mazer::GameData::Get()->CanDraw = true;

	// Construct key components
	eventManager = EventManager::Get();
	eventFactory = EventFactory::Get();
	gameCommands = std::make_shared<GameCommands>();
	inputManager = std::make_shared<InputManager>(gameCommands, verbose);

	// Subscribe to events we are interested in...
	eventManager->SubscribeToEvent(GenerateNewLevelEventId, this);
	eventManager->SubscribeToEvent(mazer::InvalidMoveEventId, this);
	eventManager->SubscribeToEvent(mazer::FetchedPickupEventId, this);
	eventManager->SubscribeToEvent(GameObjectTypeEventId, this);
	eventManager->SubscribeToEvent(SceneChangedEventTypeEventId, this);
	eventManager->SubscribeToEvent(NetworkPlayerJoinedEventId, this);
	eventManager->SubscribeToEvent(StartNetworkLevelEventId, this);
	eventManager->SubscribeToEvent(UpdateProcessesEventId, this);
	eventManager->SubscribeToEvent(GameWonEventId, this);
	eventManager->SubscribeToEvent(PlayerCollidedWithEnemyEventId, this);
	eventManager->SubscribeToEvent(PlayerDiedEventId, this);
	eventManager->SubscribeToEvent(PlayerCollidedWithPickupEventId, this);

	// Set up the network activity monitor to listen for network events
	elapsedTimeProvider = std::make_shared<ElapsedGameTimeProvider>();
	networkingActivityMonitor = std::make_shared<NetworkingActivityMonitor>(processManager, *eventManager, elapsedTimeProvider, verbose);
	networkingActivityMonitor->SetSendRateMs(sendRateMs);
	networkingActivityMonitor->Initialise();

	if (gameStatePusherEnabled) 
	{
		// Arrange for game state to be periodically sent to game server
		gameStatePusher = std::make_shared<GameStatePusher>(LevelManager::SendGameState, sendRateMs, isGameServer, processManager);
		gameStatePusher->Initialise();
		gameStatePusher->Run();
	}

	// Mark initialisation as done
	return initialized = true;
}

ListOfEvents LevelManager::HandleEvent(const std::shared_ptr<Event>& evt, const gamelib::DeltaTime* deltaTime)
{
	// Response to level changing
	if(evt->Id.PrimaryId == SceneChangedEventTypeEventId.PrimaryId) {
		OnLevelChanged(evt);
	}

	// Respond to event to update level processes
	if(evt->Id.PrimaryId == UpdateProcessesEventId.PrimaryId) { processManager.UpdateProcesses(deltaTime->milliseconds); }

	// Respond to invalid move event
	if(evt->Id.PrimaryId == mazer::InvalidMoveEventId.PrimaryId) { gameCommands->InvalidMove();}

	// Respond to player joining the game
	if(evt->Id.PrimaryId == NetworkPlayerJoinedEventId.PrimaryId) { OnNetworkPlayerJoined(evt);}

	// Respond to network game starting event
	if(evt->Id.PrimaryId == StartNetworkLevelEventId.PrimaryId) { OnStartNetworkLevel(evt); }

	// Respond to player picking up an item
	if(evt->Id.PrimaryId == mazer::FetchedPickupEventId.PrimaryId) { OnFetchedPickup(evt); }

	// Respond to player colliding with a pickup
	if (evt->Id.PrimaryId == PlayerCollidedWithPickupEventId.PrimaryId) { OnPickupCollision(evt); }

	// Respond to game won event
	if(evt->Id.PrimaryId == mazer::GameWonEventId.PrimaryId) { OnGameWon();}

	// Respond to player colliding with an enemy
	if(evt->Id.PrimaryId == PlayerCollidedWithEnemyEventId.PrimaryId) { OnEnemyCollision(evt);}

	// Respond to player dying
	if(evt->Id.PrimaryId == mazer::PlayerDiedEventId.PrimaryId) { OnPlayerDied(); }
		
	return {};
}

std::shared_ptr<InputManager> LevelManager::GetInputManager() 
{
	return inputManager;
}

void LevelManager::OnEnemyCollision(const std::shared_ptr<Event>& evt) 
{
	// Indicate that the player has collided with an enemy
	gameCommands->FetchedPickup();

	const auto collisionEvent = To<PlayerCollidedWithEnemyEvent>(evt);

	// Extract the enemies hit point value from the enemy's properties
	const auto& enemyHitPoints = collisionEvent->TheEnemy->StringProperties["HitPoint"];

	stringstream message;
	message << "Player health is " << to_string(collisionEvent->ThePlayer->GetHealth());

	// Reduce the player's health by the enemy's hit points
	collisionEvent->ThePlayer->IntProperties["Health"] -= strtol(enemyHitPoints.c_str(), nullptr, 0);

	// Print the player's health to the console
	Logger::Get()->LogThis(message.str());

	// Update the player's health (this will be reflected on the HUD)
	playerHealth->Text = to_string(collisionEvent->ThePlayer->GetHealth());

	// Check if the player is dead
	if(collisionEvent->ThePlayer->GetHealth() <= 0)
	{
		// Raise event to indicate the player has died
		eventManager->RaiseEvent(To<Event>(eventFactory->CreateGenericEvent(PlayerDiedEventId, GetSubscriberName())), this);
	}
}

void LevelManager::OnPickupCollision(const std::shared_ptr<Event>& evt) const
{
	// Extract collision event details
	const auto collisionEvent = To<PlayerCollidedWithPickupEvent>(evt);

	// Extract the pickup value from the pickup's properties
	const auto& pickupValue = collisionEvent->ThePickup->StringProperties["value"];
		
	// Update the player's points (Increase them on collision with the pickup)
	collisionEvent->ThePlayer->IntProperties["Points"] += strtol(pickupValue.c_str(), nullptr, 0);

	// Print the player's points to the console
	stringstream message;
	message << "Player gain " << to_string(collisionEvent->ThePlayer->GetPoints()) << " points";

	playerPoints->Text = to_string(collisionEvent->ThePlayer->GetPoints());

	Logger::Get()->LogThis(playerPoints->Text);
}

void LevelManager::OnFetchedPickup(const std::shared_ptr<Event>& evt) const
{
	// Indicate that the player has fetched a pickup
	gameCommands->FetchedPickup();

	// Show a different image every time a pickup is collected
	hudItem->AdvanceFrame();	
}

void LevelManager::OnPlayerDied()
{
	Logger::Get()->LogThis("Player DIED!");
}

void LevelManager::OnStartNetworkLevel(const std::shared_ptr<Event>& evt)
{
	// Always start the game on level 1 when the network game starts
	auto _ = ChangeLevel(1);

	// Get level 1 file
	const auto levelFile = GetSetting("global", "level1FileName");

	// Create the level from the level file
	CreateLevel(levelFile);
}

string LevelManager::GetSetting(const std::string& section, const std::string& settingName)
{
	return SettingsManager::Get()->GetString(section, settingName);
}

int LevelManager::GetIntSetting(const std::string& section, const std::string& settingName)
{
	return SettingsManager::Get()->GetInt(section, settingName);
}

bool LevelManager::GetBoolSetting(const std::string& section, const std::string& settingName)
{
	return SettingsManager::Get()->GetBool(section, settingName);
}

void LevelManager::RemoveAllGameObjects()
{
	const auto condition = [this](const std::weak_ptr<GameObject>& gameObject)
	{
		// Game object must have expired 
		if (!gameObject.expired())
		{
			eventManager->RaiseEvent(GameObjectEventFactory::MakeRemoveObjectEvent(gameObject.lock()), this);
		}
	};

	// Iterate through all game object and raise event to remove them
	ranges::for_each(GameData::Get()->GameObjects, condition);

	// Remove all pickups also
	pickups.clear();

	// Also remove player reference
	player = nullptr;
}

void LevelManager::OnLevelChanged(const std::shared_ptr<Event>& evt)
{
	// Change the music based on the level
	switch(To<SceneChangedEvent>(evt)->SceneId)
	{
		case 1: PlayLevelMusic("LevelMusic1"); break;
		case 2: PlayLevelMusic("LevelMusic2"); break;
		case 3: PlayLevelMusic("LevelMusic3"); break;
		case 4: PlayLevelMusic("LevelMusic4"); break;
		case 5: PlayLevelMusic("LevelMusic5"); break;

		// Reached the end of the level list, so play the auto level music
		default: PlayLevelMusic("AutoLevelMusic"); break;
	}
}

void LevelManager::PlayLevelMusic(const std::string& levelMusicAssetName)
{
	// Get details about the level music asset
	const auto asset = ResourceManager::Get()->GetAssetInfo(levelMusicAssetName);

	// Play the level music (if it is loaded)
	if (asset && asset->IsLoadedInMemory)
	{
		AudioManager::Get()->Play(asset);
	}	
}

void LevelManager::OnGameWon()
{
	// Prepare some actions to run as processes in the process manger when the game is won
	const auto turnOffMusic = std::static_pointer_cast<Process>(std::make_shared<Action>([&](unsigned long deltaMs) { gameCommands->ToggleMusic(verbose); }));
	const auto playWinMusic = std::static_pointer_cast<Process>(std::make_shared<Action>([&](unsigned long deltaMs)
	{
		// Play win music asset
		AudioManager::Play(ResourceManager::Get()->GetAssetInfo(SettingsManager::String("audio", "win_music")));
	}));
	
	const auto wait = std::static_pointer_cast<Process>(std::make_shared<DelayProcess>(5000));

	const auto loadNextLevel = std::static_pointer_cast<Process>(std::make_shared<Action>([&](unsigned long deltaMs)
	{
		// Load the next level
		gameCommands->LoadNewLevel(static_cast<int>(++currentLevel));
	}));

	// Chain the game win sequence as a list of processes
	turnOffMusic->AttachChild(playWinMusic)->AttachChild(wait)->AttachChild(loadNextLevel);

	// Put it on the process manager so that it will be run
	processManager.AttachProcess(turnOffMusic);

	Logger::Get()->LogThis("All Pickups Collected Well Done!");
}

void LevelManager::GetKeyboardInput(const gamelib::DeltaTime* deltaTime) const
{
	inputManager->Sample(deltaTime);
}

void LevelManager::CreatePlayer(const std::vector<std::shared_ptr<mazer::Room>> &rooms, const int resourceId) 
{
	// Get the player's name from the settings file
	const auto playerNickName = GameData::Get()->IsNetworkGame
	?  GetSetting("networking", "nickname")
	    : "Player1";

	// Create the player character from the player's definition
	player = CharacterBuilder::BuildPlayer(playerNickName, GetRandomRoom(rooms), resourceId, playerNickName);

	// Don't show the player if disableCharacters is set
	if(disableCharacters)
	{
		return;	
	}

	AddGameObjectToScene(player);
}

shared_ptr<mazer::Room> LevelManager::GetRandomRoom(const std::vector<std::shared_ptr<mazer::Room>>& rooms)
{
	return rooms[GetRandomIndex(0, static_cast<int>(rooms.size()) - 1)];
}

void LevelManager::CreateAutoPickups(const vector<shared_ptr<mazer::Room>>& rooms)
{
	// Get the default number of pickups to create from the settings file
	const auto numPickups = GetIntSetting("global", "numPickups");

	// Distinguish the number of pickups to create for each type 
	const auto part = numPickups / 3;

	// Don't show any pickups if disableCharacters is set
	if(disableCharacters)
	{
		return;
	}

	// Create the different types of pickups and place them in random rooms
	for(auto i = 0; i < numPickups; i++)
	{
		const auto& randomRoom = GetRandomRoom(rooms);
		const auto pickupName = string("RoomPickup") + std::to_string(randomRoom->GetRoomNumber());
		
		std::shared_ptr<SpriteAsset> spriteAssert;

		// Place 3 sets evenly of each type of pickup	
		if(i < 1 * part)
		{
			spriteAssert = To<SpriteAsset>(ResourceManager::Get()->GetAssetInfo(GetSetting("pickup1", "assetName")));
		}
		else if(i >= 1 * part && i < 2 * part)
		{
			spriteAssert = To<SpriteAsset>(ResourceManager::Get()->GetAssetInfo(GetSetting("pickup2", "assetName")));
		}
		else if(i >= 2 * part)
		{
			spriteAssert = To<SpriteAsset>(ResourceManager::Get()->GetAssetInfo(GetSetting("pickup3", "assetName")));
		}

		auto pickup = CharacterBuilder::BuildPickup(pickupName, randomRoom, spriteAssert->Uid);
		
		pickups.push_back(pickup);
	}

	// Set up the pickups
	InitializeAutoPickups(pickups);
}

void LevelManager::AddScreenWidgets(const std::vector<std::shared_ptr<mazer::Room>>& rooms)
{
	AddGameObjectToScene(CreateHud(rooms, player));	
	AddGameObjectToScene(CreateDrawableFrameRate());

	playerHealth = CreateDrawablePlayerHealth();
	playerPoints = CreateDrawablePlayerPoints();	
	
	AddGameObjectToScene(playerHealth);
	AddGameObjectToScene(playerPoints);
}

void LevelManager::CreateLevel(const string& levelFilePath)
{	
	RemoveAllGameObjects();

	// Load the level definition file
	level = std::make_shared<mazer::Level>(levelFilePath);

	LogMessage(std::string("Loading level ") + levelFilePath + "...", true);

	level->Load();

	// Setup rooms in the level
	const auto& rooms = level->Rooms;

	// Initialize the rooms in the level
	InitializeRooms(rooms);	

	// Add player to room in level
	CreatePlayer(rooms, GetAsset("edge_player")->Uid);

	// Automatically create random pickups if the level file has been set to do so
	if (level->IsAutoPopulatePickups())
	{
		CreateAutoPickups(rooms);
	}

	// Add everything to the scene
	AddScreenWidgets(rooms);
}

std::shared_ptr<DrawableFrameRate> LevelManager::CreateDrawableFrameRate()
{
	// We'll be placing the frame rate in the top right corner of the screen
	constexpr auto firstRow = 1;
	const int lastColumn = level->NumCols;

	// Use the area of the room in the top right corner of the screen as the drawable area
	auto widgetArea = &level->GetRoom(firstRow,lastColumn)->Bounds;

	// Create the drawable frame rate object
	drawableFrameRate = std::make_shared<DrawableFrameRate>(widgetArea);

	return drawableFrameRate;
}

std::shared_ptr<DrawableText> LevelManager::CreateDrawablePlayerHealth() const
{
	// We'll be placing the health in the bottom left corner of the screen
	const auto lastRow = level->NumRows;
	const auto healthRoom = level->GetRoom(lastRow,1);
	const auto amount = healthRoom->InnerBounds.h / 2;	
	auto colour = SDL_Color {255,0,0,0}; // Red

	healthRoom->InnerBounds.h /= 2;
	healthRoom->InnerBounds.y += amount/2;

	return make_shared<DrawableText>(healthRoom->InnerBounds, std::to_string(player->GetHealth()), colour);

}

std::shared_ptr<DrawableText> LevelManager::CreateDrawablePlayerPoints() const
{
	// We'll be placing the points in the bottom right corner of the screen
	const auto lastRow = level->NumRows;
	const int lastColumn = level->NumCols;
	const auto pointsRoom = level->GetRoom(lastRow,lastColumn);
	const auto amount = pointsRoom->InnerBounds.h / 2;	
	auto colour = SDL_Color {0,0,255,0}; // Blue

	pointsRoom->InnerBounds.h /= 2;
	pointsRoom->InnerBounds.y += amount/2;

	return make_shared<DrawableText>(pointsRoom->InnerBounds, std::to_string(player->GetPoints()), colour);
}

std::shared_ptr<StaticSprite> LevelManager::CreateHud(const std::vector<std::shared_ptr<mazer::Room>>& rooms,
                                                      const std::shared_ptr<mazer::Player>& inPlayer)
{
	// We'll be placing the hud in the top left corner of the screen
	constexpr auto firstRow = 1;
	constexpr auto firstCol = 1;
	const auto hudPosition = level->GetRoom(firstRow, firstCol)->GetCenter(inPlayer->GetWidth(), inPlayer->GetHeight());
	const auto hudAsset = GetAsset("hudspritesheet");

	// Build it
	hudItem = GameObjectFactory::Get().BuildStaticSprite(hudAsset, hudPosition);

	// Initialise it
	InitializeHudItem(hudItem);

	return hudItem;
}

void LevelManager::InitializeHudItem(const std::shared_ptr<StaticSprite>& hudItem)
{
	hudItem->LoadSettings();
	hudItem->SubscribeToEvent(PlayerMovedEventTypeEventId);
}

void LevelManager::AddGameObjectToScene(const std::shared_ptr<GameObject>& gameObject)
{
	// Schedule for the game object to be added to the scene
	eventManager->RaiseEvent(To<Event>(eventFactory->CreateAddToSceneEvent(gameObject)), this);
}

void LevelManager::CreateAutoLevel()
{
	// Parse the resource file to categorise the assets
	RemoveAllGameObjects();
	
	const auto _ = Get()->ChangeLevel(1);
		
	level = std::make_shared<Level>();
	level->Load(); // construct a level

	InitializeRooms(level->Rooms);
	CreatePlayer(level->Rooms, GetAsset("edge_player")->Uid);
	CreateAutoPickups(level->Rooms);

	AddScreenWidgets(level->Rooms);
}

void LevelManager::InitializePlayer(const std::shared_ptr<mazer::Player>& inPlayer, const std::shared_ptr<SpriteAsset>&spriteAsset)
{
	inPlayer->SetMoveStrategy(std::make_shared<GameObjectMoveStrategy>(inPlayer, inPlayer->CurrentRoom));
	inPlayer->SetTag(gamelib::PlayerTag);
	inPlayer->LoadSettings();
	inPlayer->SetSprite(AnimatedSprite::Create(inPlayer->Position, spriteAsset));

	// We keep a reference to track of the player globally
	GameData::Get()->player = inPlayer;
}

void LevelManager::InitializeAutoPickups(const std::vector<std::shared_ptr<mazer::Pickup>>& inPickups)
{
	for (const auto& pickup : inPickups)
	{		
		pickup->StringProperties["value"] = "1"; // The value of each pickup if 1 point but this value could be read from config
		AddGameObjectToScene(pickup);
	}
}

void LevelManager::InitializeRooms(const std::vector<std::shared_ptr<mazer::Room>>& rooms)
{
	for (const auto& room : rooms)
	{
		room->Initialize();
		AddGameObjectToScene(room);
	}
}

// Raises change level event
bool LevelManager::ChangeLevel(const int levelNum) const
{
	bool changedLevel = false;
	try
	{
		gameCommands->RaiseChangedLevel(false, static_cast<short>(levelNum));
		changedLevel = true;
	}
	catch (exception &e)
	{
		std::stringstream message;
		message << "Could not start level reason="<< e.what() << ", level=" << std::to_string(levelNum);
		LogMessage(message.str());		
	}
	return changedLevel;
}

void LevelManager::OnNetworkPlayerJoined(const std::shared_ptr<Event>& evt)
{
	// We know a player has joined the game server.
	const auto networkPlayerJoinedEvent = To<NetworkPlayerJoinedEvent>(evt);
	
	// Add the player to our level... find a suitable position for it
}

Mix_Chunk* LevelManager::GetSoundEffect(const std::string& name)
{
	return AudioManager::ToAudioAsset(GetAsset(name))->AsSoundEffect();
}

std::shared_ptr<Asset> LevelManager::GetAsset(const std::string& name)
{
	return ResourceManager::Get()->GetAssetInfo(name);
}

shared_ptr<Level> LevelManager::GetLevel() { return level; }

inline std::string LevelManager::GetSubscriberName()
{
	return "level_manager";
}

void LevelManager::SendGameState()
{
	// We'll broadcast our state as pings to the server
	GameCommands::PingGameServer(0);
}

void LevelManager::ScheduleProcess(std::shared_ptr<Process> process)  // NOLINT(performance-unnecessary-value-param)
{
	processManager.AttachProcess(process);
}

LevelManager* LevelManager::Get() { if (instance == nullptr) { instance = new LevelManager(); } return instance; }
LevelManager::~LevelManager()
{
	instance = nullptr;
}
LevelManager* LevelManager::instance = nullptr;