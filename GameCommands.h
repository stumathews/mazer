#pragma once
#include "file/SettingsManager.h"
#include "events/EventManager.h"
#include <events/EventSubscriber.h>
#include <memory>
#include "audio/AudioManager.h"
#include <resource/ResourceManager.h>
#include <events/Event.h>
#include "events/ControllerMoveEvent.h"
#include "objects/GameObject.h"

class GameCommands final : public gamelib::EventSubscriber, public std::enable_shared_from_this<GameCommands>
{
public:	
	GameCommands();
	~GameCommands() override = default;
	GameCommands(const GameCommands& copy) = delete;
	GameCommands(const GameCommands&& copy) = delete;
	GameCommands& operator=(const GameCommands& other) = delete;
	GameCommands& operator=(const GameCommands&& other) = delete;

	std::string GetSubscriberName() override;

	void Fire(bool beVerbose);
	void MoveUp(bool beVerbose, gamelib::ControllerMoveEvent::KeyState keyState);
	void MoveDown(bool beVerbose, gamelib::ControllerMoveEvent::KeyState keyState);
	void MoveLeft(bool beVerbose, gamelib::ControllerMoveEvent::KeyState keyState);
	void MoveRight(bool beVerbose, gamelib::ControllerMoveEvent::KeyState keyState);
	void Move(gamelib::Direction direction, gamelib::ControllerMoveEvent::KeyState keyState);
	void PlaySoundEffect(const std::shared_ptr<gamelib::AudioAsset>& effect) const;
	void RaiseChangedLevel(bool beVerbose, short newLevel);
	void ReloadSettings(bool beVerbose);
	void LoadNewLevel(int level);
	void ToggleMusic(bool beVerbose) const;
	void Quit(bool beVerbose) const;
	void InvalidMove(bool beVerbose = false) const;
	void FetchedPickup(bool beVerbose = false) const;
	void StartNetworkLevel();
	static void PingGameServer(const gamelib::DeltaTime* deltaTime);
private:
	bool verbose;
	bool logCommands;

	// Inherited via EventSubscriber
	gamelib::ListOfEvents HandleEvent(const std::shared_ptr<gamelib::Event>& evt, const gamelib::DeltaTime* deltaTime) override;
};


