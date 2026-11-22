// fceuxs5 frontend: the menus, drawn on the 1920x1080 surface.
// SPDX-License-Identifier: MIT
#pragma once

#include "fe_games.h"

#include <string>

namespace fe
{
enum class PauseAction
{
	Resume,
	BackToList,
	Quit,
};
// The in-game menu (L3 + R3), over the paused picture.
PauseAction PauseMenu();

// The settings screen (Triangle on the shelf, or from the pause menu).
void SettingsMenu();
// 1.1: Settings over the running game (the touchpad); Circle goes back to the game.
void InGameSettings();
// 1.2: the version an update installed when the player chose to close the app for it (Settings, Updates,
// "Installed: close and open again"); "" otherwise. The shelf and the pause menu then end the app.
std::string QuitForUpdate();

// Settings > Network shares: the SMB and NFS shares games are read from (fe_netscreen.cpp, from Snes9x PS5).
void NetworkMenu();

// fceuxs5: a game's options (Square in the library): its ROM hack (fe_patch.h), its box art again, play.
// *refetch_cover: the player asked for the box art again.
enum class GameAction
{
	None,
	Play,
};
GameAction GameOptions(const GameInfo& game, bool* refetch_cover);

// Shows a message box until Cross / Circle is pressed (errors such as a game that won't start).
void MessageBox(const std::string& title, const std::string& text);
} // namespace fe
