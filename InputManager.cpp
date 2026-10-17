#include "InputManager.h"
#include <cppgamelib/exceptions/EngineException.h>

gamelib::ControllerMoveEvent::KeyState GetKeyState(const SDL_Event& e)
{
	return e.type == SDL_KEYDOWN 
						? gamelib::ControllerMoveEvent::KeyState::Pressed
						: gamelib::ControllerMoveEvent::KeyState::Released;
}

// Fetches input from the SDL event queue and translates it into game commands. This function is called automatically every frame by the game loop.
void InputManager::Sample(const unsigned long deltaMs)
{
	if(gameCommands == nullptr)
	{
		THROW(12, "No game commands set on the input manager. No input will be sampled.", "InputManager");
	}

	SDL_Event e;
	while(SDL_PollEvent(&e))
	{
		if (e.type == SDL_QUIT) 
		{ 
			gameCommands->Quit(verbose); return;
		}
		if (e.key.repeat == 0) // we ignore key repeats
		{
			switch(e.key.keysym.sym)
			{
				case SDLK_w:
				case SDLK_UP:
					gameCommands->MoveUp(verbose, GetKeyState(e));
					break;
		        case SDLK_s:
		        case SDLK_DOWN:
					gameCommands->MoveDown(verbose, GetKeyState(e));
		            break;
		        case SDLK_a:
		        case SDLK_LEFT:
					gameCommands->MoveLeft(verbose, GetKeyState(e));
		            break;
		        case SDLK_d:
		        case SDLK_RIGHT:
					gameCommands->MoveRight(verbose, GetKeyState(e));
		            break;
            default: ;
			}
		}
	}

	// Input -> Game Commands

	const auto keyState = SDL_GetKeyboardState(nullptr);

	if (keyState[SDL_SCANCODE_Q] || keyState[SDL_SCANCODE_ESCAPE]) { gameCommands->Quit(verbose); }
	if (keyState[SDL_SCANCODE_R]) { gameCommands->ReloadSettings(verbose);  }
	if (keyState[SDL_SCANCODE_P]) { gameCommands->PingGameServer(0);  }
	if (keyState[SDL_SCANCODE_R]) { gameCommands->ReloadSettings(verbose); }
	if (keyState[SDL_SCANCODE_R]) { gameCommands->ReloadSettings(verbose);  }
	if (keyState[SDL_SCANCODE_N]) { gameCommands->StartNetworkLevel();  }
	if (keyState[SDL_SCANCODE_SPACE]) { gameCommands->Fire(verbose);  }
	if (keyState[SDL_SCANCODE_0]) { gameCommands->ToggleMusic(false);  }	
}