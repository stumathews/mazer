#include "GameCommands.h"
#include <memory>
#include <cppgamelib/events/ControllerMoveEvent.h>
#include <cppgamelib/events/SceneChangedEvent.h>
#include <cppgamelib/scene/SceneManager.h>
#include <cppgamelib/events/NetworkTrafficReceivedEvent.h>
#include <sstream>
#include <cppgamelib/net/NetworkManager.h>
#include <cppgamelib/events/StartNetworkLevelEvent.h>
#include "LevelManager.h"
#include <GameData.h>
#include <SDL_mixer.h>
#include <file/SettingsManager.h>
#include "EventNumber.h"
#include <cppgamelib/events/NetworkPlayerJoinedEvent.h>
#include <cppgamelib/utils/Utils.h>
#include <mazer/GameDataManager.h>

#include "file/Logger.h"

using namespace gamelib;
using namespace std;

GameCommands::GameCommands()
{
	verbose = SettingsManager::Bool("global", "verbose");
	logCommands = SettingsManager::Bool("global", "verbose");
	
	//EventManager::Get()->SubscribeToEvent(PlayerMovedEventTypeEventId, this);
}

std::string GameCommands::GetSubscriberName()
{
	return "GameCommands";
}

void GameCommands::Fire(const bool beVerbose)
{
	if (logCommands) { Logger::Get()->LogThis("GameCommand: Fire", beVerbose); }
	
	PlaySoundEffect(AudioManager::ToAudioAsset(ResourceManager::Get()->GetAssetInfo("scratch.wav")));
	
	EventManager::Get()->RaiseEvent(EventFactory::Get()->CreateGenericEvent(mazer::FireEventId, GetSubscriberName()), this);
}

void GameCommands::MoveUp(const bool beVerbose, const ControllerMoveEvent::KeyState keyState)
{
	if (logCommands) { Logger::Get()->LogThis("GameCommand: MoveUp", beVerbose); }

	Move(Direction::Up, keyState);
}

void GameCommands::MoveDown(const bool beVerbose, const ControllerMoveEvent::KeyState keyState)
{
	if (logCommands) { Logger::Get()->LogThis("GameCommand: MoveDown", beVerbose); }

	Move(Direction::Down, keyState);
}

void GameCommands::MoveLeft(const bool beVerbose, const ControllerMoveEvent::KeyState keyState)
{
	if (logCommands) { Logger::Get()->LogThis("GameCommand: MoveLeft", beVerbose); }

	Move(Direction::Left, keyState);
}

void GameCommands::MoveRight(const bool beVerbose, const ControllerMoveEvent::KeyState keyState)
{
	if (logCommands) { Logger::Get()->LogThis("GameCommand: MoveRight", beVerbose); }

	Move(Direction::Right, keyState);
}

void GameCommands::Move(const Direction direction, ControllerMoveEvent::KeyState keyState = ControllerMoveEvent::KeyState::Pressed)
{
	
	switch(direction)
	{
		case Direction::Up: EventManager::Get()->RaiseEvent(EventFactory::Get()->CreateControllerMoveEvent(Direction::Up, keyState), this); 	break;
		case Direction::Down: EventManager::Get()->RaiseEvent(EventFactory::Get()->CreateControllerMoveEvent(Direction::Down, keyState), this); break;
		case Direction::Left: EventManager::Get()->RaiseEvent(EventFactory::Get()->CreateControllerMoveEvent(Direction::Left, keyState), this); 	break;
		case Direction::Right: EventManager::Get()->RaiseEvent(EventFactory::Get()->CreateControllerMoveEvent(Direction::Right, keyState), this); break;
		case Direction::None: THROW(12, "Unknown direction", "GameCommands");
	}
}

void GameCommands::PlaySoundEffect(const shared_ptr<AudioAsset>& effect) const
{
	if (logCommands) { Logger::Get()->LogThis("GameCommand: PlaySoundEffect", verbose); }

	AudioManager::Get()->Play(effect);
}

void GameCommands::RaiseChangedLevel(const bool beVerbose, const short newLevel)
{
	if (logCommands) { Logger::Get()->LogThis("GameCommand: ChangeLevel", beVerbose); }

	EventManager::Get()->RaiseEvent(EventFactory::Get()->CreateSceneChangedEventEvent(newLevel), this);
}

void GameCommands::ReloadSettings(const bool beVerbose)
{
	if (logCommands) { Logger::Get()->LogThis("GameCommand: ReloadSettings", beVerbose); }

	SettingsManager::Get()->Reload();

	EventManager::Get()->RaiseEvent(EventFactory::Get()->CreateGenericEvent(mazer::SettingsReloadedEventId, {}), this);
}

void GameCommands::LoadNewLevel(const int level)
{
	switch (level)
	{
		case 1: { LevelManager::Get()->CreateLevel(SettingsManager::String("global", "level1FileName")); } break;
		case 2: { LevelManager::Get()->CreateLevel(SettingsManager::String("global", "level2FileName")); } break;
		case 3: { LevelManager::Get()->CreateLevel(SettingsManager::String("global", "level3FileName")); } break;
		case 4: { LevelManager::Get()->CreateLevel(SettingsManager::String("global", "level4FileName")); } break;
		case 5: { LevelManager::Get()->CreateLevel(SettingsManager::String("global", "level5FileName")); } break;
		default:
			{
				LevelManager::Get()->CreateAutoLevel();
			} break;
	}

	// Send event to change level to selected level
	RaiseChangedLevel(verbose, static_cast<short>(level));	
}

void GameCommands::ToggleMusic(const bool beVerbose) const
{
	if (logCommands) { Logger::Get()->LogThis("GameCommand: ToggleMusic", beVerbose); }

	if (!Mix_PlayingMusic() && !Mix_PausedMusic())
	{
		// We always plat level 4's music if no music is playing
		AudioManager::Get()->Play(AudioManager::ToAudioAsset(ResourceManager::Get()->GetAssetInfo("LevelMusic4")));
	}
		
	Mix_PausedMusic() == 1 ? Mix_ResumeMusic() : Mix_PauseMusic();	
}

void GameCommands::Quit(const bool beVerbose) const
{
	if (logCommands) { Logger::Get()->LogThis("GameCommand: Quitting", beVerbose); }

	mazer::GameDataManager::Get()->GameWorldData.IsGameDone = true;
}

void GameCommands::InvalidMove(const bool beVerbose) const
{
	if (logCommands) { Logger::Get()->LogThis("GameCommand: Invalid move!", beVerbose); }
}

void GameCommands::FetchedPickup(const bool beVerbose) const
{
	if (logCommands) { Logger::Get()->LogThis("GameCommand: FetchedPickup", beVerbose); }

	AudioManager::Get()->Play(ResourceManager::Get()->GetAssetInfo(SettingsManager::String("audio", "fetched_pickup")));
}

void GameCommands::StartNetworkLevel()
{
	// This only works on the Game server
	if(mazer::GameData::Get()->IsMultiPlayerGame()) { return; }

	if (logCommands) { Logger::Get()->LogThis("GameCommand: StartNetworkLevel", verbose); }

	// Ask the LevelManager to prepare a level description and pass that to StartNetworkLevelEvent
	// and let that propagate to all players

	EventManager::Get()->RaiseEvent(EventFactory::Get()->CreateStartNetworkLevelEvent(1), this);
}

void GameCommands::PingGameServer(const gamelib::DeltaTime* deltaTime) { NetworkManager::Get()->PingGameServer(deltaTime); }

ListOfEvents GameCommands::HandleEvent(const std::shared_ptr<Event>& evt, const gamelib::DeltaTime* deltaTime)
{	
	// Consider handling all game level events in LevelManager.cpp which then call into GameCommands.cpp
	return {};
}