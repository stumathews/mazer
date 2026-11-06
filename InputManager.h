#pragma once
#include "GameCommands.h"
#include "input/IInputManager.h"

class InputManager final : public gamelib::IInputManager
{
public:
	explicit InputManager(std::shared_ptr<GameCommands> gameCommands, const bool verbose)
	: gameCommands(std::move(gameCommands)), verbose(verbose) {}
	
	void Sample(const gamelib::DeltaTime* deltaTime) override;

private:
	
	std::shared_ptr<GameCommands> gameCommands;
	const bool verbose;
};

