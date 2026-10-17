//#include "SDL.h"
#include <vector>
#include <cppgamelib\file\Logger.h>
#include <cppgamelib\file\SettingsManager.h>
#include <cppgamelib\file/TextFile.h>
#include <cppgamelib\structure/GameStructure.h>
#include <cppgamelib\structure/FixedStepGameLoop.h>
#include <cppgamelib/objects/GameWorldData.h>
#include <direct.h>
#include <GameData.h>
#include <GameDataManager.h>
#include <iostream>
#include <common/Common.h>
#include <events/AddGameObjectToCurrentSceneEvent.h>
#include <events/PlayerMovedEvent.h>
#include <events/UpdateProcessesEvent.h>
#include <Logging/ErrorLogManager.h>
#include <testlib/messages.h>
#include <mazer/Enemy.h>
#include <mazer/Room.h>
#include <net/NetworkManager.h>
#include <scene/SceneManager.h>
#include <cppgamelib/events/AddGameObjectToCurrentSceneEvent.h>
#include <cppgamelib/events/UpdateAllGameObjectsEvent.h>
#include <direct.h>

#include "LevelManager.h"
#include <mazer/EnemyMovedEvent.h>

using namespace std;

using namespace std;
using namespace gamelib;

typedef SettingsManager Settings;

namespace
{
	/*
		##   ##    ##     ### ##   ### ###  ### ##            ## ##    ### ##
		## ##      ##    ##  ##    ##  ##   ##  ##           ##  ##    ##  ##
		# ### #   ## ##      ##     ##       ##  ##               ##    ##  ##
		## # ##   ##  ##    ##      ## ##    ## ##               ##     ##  ##
		##   ##   ## ###   ##       ##       ## ##              ##      ##  ##
		##   ##   ##  ##  ##  ##    ##  ##   ##  ##            #   ##   ##  ##
		##   ##  ###  ##  # ####   ### ###  #### ##           ######   ### ##
	*/

	static void InitializeGameSubSystems(GameStructure& gameStructure);
	static void PrepareFirstLevel();
	static void GetInput(unsigned long deltaMs);

	void PrepareFirstLevel()
	{
		const auto isSinglePlayerGame = mazer::GameData::Get()->IsSinglePlayerGame();

		if (isSinglePlayerGame)
		{
			auto _ = LevelManager::Get()->ChangeLevel(1);

			if (Settings::Bool("global", "createAutoLevel"))
			{
				LevelManager::Get()->CreateAutoLevel();
			}
			else
			{
				// Get the name of the level file for the first level
				const auto firstLevelFilePath = Settings::String("global", "level1FileName");

				LevelManager::Get()->CreateLevel(firstLevelFilePath);
			}
		}

		Logger::Get()->LogThis(!isSinglePlayerGame
			? NetworkManager::Get()->IsGameServer()
			? "Waiting for network players. Press 'n' to start network game."
			: "Waiting for the server to start the network game to begin."
			: "Creating Single player level...");
	}

	// Warms up the game subsystems, including logging, level manager, and network manager. This is called once at the start of the game.
	void InitializeGameSubSystems(GameStructure& gameStructure)
	{
		constexpr auto screenWidth = 0; // 0 will mean it will get read from the settings file
		constexpr auto screenHeight = 0; // 0 will mean it will get read from the settings file
		constexpr auto windowTitle = "Mazer2D!";
		constexpr auto resourcesFilePath = "conf\\Resources.xml"; // relative to output binary folder
		constexpr auto sceneFolderPath = "conf\\";

		// Check game settings to determine if this is a network game
		const auto isNetworkGame = SettingsManager::Get()->GetBool("global", "isNetworkGame");
		mazer::GameDataManager::Get()->Initialize(isNetworkGame);

		// Initialize the logging system
		ErrorLogManager::GetErrorLogManager()->Create("GameErrors.txt");

		// Initialize game structure
		const auto isGameStructureInitialized = gameStructure.Initialize(
			screenWidth, screenHeight, windowTitle, resourcesFilePath,
			sceneFolderPath);

		// Initialize level manager
		const auto isLevelManagerInitialized = LevelManager::Get()->Initialize();

		const auto anyInitFailures = !IsSuccess(isGameStructureInitialized, "Successfully initialized game structure.") ||
			!IsSuccess(isLevelManagerInitialized, "Successfully initialized Level Manager...");

		// Check for any initialization failures
		if (anyInitFailures)
		{
			const auto verboseLoggingEnabled = Settings::Bool("global", "verbose");

			LogMessage("Game subsystem initialization failed.", verboseLoggingEnabled, true);

			THROW(12, "There was a problem initializing the game subsystems", "Initialize Subsystems");
		}
	}

	// Runs each frame to update the game state. This is called automatically by the game loop.
	static void Update(const unsigned long deltaMs)
	{
		// Execute 'draw' event's subscribers immediately
		EventManager::Get()->DispatchEventToSubscriber(EventFactory::Get()->CreateUpdateAllGameObjectsEvent(), deltaMs);

		//Execute 'update processes' event's subscribers immediately.
		EventManager::Get()->DispatchEventToSubscriber(EventFactory::Get()->CreateUpdateProcessesEvent(), deltaMs);

		// For each event in the event queue, call all event's subscribers iteratively.
		EventManager::Get()->ProcessAllEvents(deltaMs);  // Must not take longer than the time step, or the game will lag. If it does, consider increasing the time step.
	}

	// Runs during each frame to draw the current scene. This is called automatically by the game loop.
	static void Draw()
	{
		// Time-sensitive, skip queue. Execute draw event's subscribers immediately. This is because we want to draw the current scene as quickly as possible, and not wait for the event queue to be processed.
		EventManager::Get()->DispatchEventToSubscriber(EventFactory::Get()->CreateGenericEvent(DrawCurrentSceneEventId, "Game"), 0UL);
	}

	void GetInput(const unsigned long deltaMs)
	{
		// Sample input events from the SDL event queue and translate them into game commands. This is called automatically by the game loop.
		LevelManager::Get()->GetInputManager()->Sample(deltaMs);

		// If this is a network game, listen for network messages from the server or clients. Will create networking events. This is called automatically by the game loop.
		NetworkManager::Get()->Listen(mazer::GameDataManager::Get()->GameWorldData.ElapsedGameTime);
	}
}

int main(int, char* [])
{
	try
	{
		// Load settings file
		if (!SettingsManager::Get()->ReadSettingsFile("conf\\settings.xml"))
		{
			char cwd[1024];
			if (_getcwd(cwd, sizeof(cwd)) != nullptr)
			{
				std::cout << "Current working directory: " << cwd << "\n";
			}
			else
			{
				std::cout << "Failed to get current working directory.\n";

				THROW(1, "Could not load settings file", "Settings");
			}
		}

		// Initialize the game infrastructure: loop strategy , update, draw, and input functions
		GameStructure infrastructure(std::make_shared<FixedStepGameLoop>(16, Update, Draw, GetInput));

		// Initialize game subsystems: logging, level manager, network manager, etc.
		InitializeGameSubSystems(infrastructure);

		// Load level and create/add game objects
		PrepareFirstLevel();

		// Start the game loop. This will call the Update and Draw functions every frame, as well as the GetInput function to sample input events.
		// This will pump update/draw events onto the event system, which level objects subscribe to
		infrastructure.DoGameLoop(&mazer::GameDataManager::Get()->GameWorldData);

		// Game is finished, unload subsystems

		auto isUnloaded = infrastructure.Unload();

		return IsSuccess(isUnloaded, "Unloading game subsystems successful.");
	}
	catch (const EngineException& e)
	{
		//MessageBoxA(nullptr, e.what(), "Game Error!", MB_OK);
		std::cout << "Game Error: " << e.what() << "\n";
		ErrorLogManager::GetErrorLogManager()->Buffer << "****ERROR****\n";
		ErrorLogManager::GetErrorLogManager()->Flush();
		ErrorLogManager::GetErrorLogManager()->LogException(e);
		ErrorLogManager::GetErrorLogManager()->Buffer << "*************\n";
		ErrorLogManager::GetErrorLogManager()->Flush();
	}
	return 0;
}