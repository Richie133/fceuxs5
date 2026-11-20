// fceuxs5 frontend: the three interfaces the player chooses from (Settings > Interface, and at the first start).
//
//   Channel  a console-channel style: a framed game list on the left, the selected game's box art on the right,
//            rounded Settings / Exit buttons, light greys with red frames.
//   Station  a sky-and-clouds style: a big multicoloured title, a translucent list of game names in capitals over
//            rolling hills, the box art beside it.
//   Flow     a cover flow: the box art of the library in 3D (the shelf, fe_shelf.cpp) on a light floor, an icon bar
//            on top and a rounded panel at the bottom with the title, the clock and the game count.
//
// All the art is drawn by code (fe_draw.h) with open fonts (Varela Round, Bungee: SIL OFL); nothing is taken from
// another program's interface.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fe_covers.h"
#include "fe_games.h"

#include <string>
#include <vector>

namespace fe
{
enum class Theme : int
{
	Channel = 0,
	Station = 1,
	Flow = 2,
	Count = 3
};

const char* ThemeName(Theme t);  // "Channel"
const char* ThemeBlurb(Theme t); // one line for the picker
Theme CurrentTheme();            // Config().theme
// The theme's font and the colours of the menus (fe_ui.h's palette); call after the setting changes.
void ApplyTheme();
// The picker: three previews, Left / Right and Cross. first_time: no Circle to back out. Returns true when one was
// chosen (Config().theme set, theme_chosen saved).
bool ThemePicker(bool first_time);

// What the launcher shows, for the theme to draw.
struct LauncherView
{
	const std::vector<GameInfo>* games = nullptr; // the list on screen
	const std::vector<CoverPtr>* covers = nullptr; // its box art, by index (null: not loaded yet)
	int sel = 0;
	float pos = 0; // the selection, gliding (for scrolling)
	std::string storage;  // "Internal", "USB", "Network"
	std::string family;   // "All games", "A"...
	std::string status;   // "Downloading covers... 12 left"
	std::string patch;    // the ROM hack the selected game starts with, "" none
	std::vector<std::string> empty_lines; // when there are no games: what to say
	int total = 0;        // games in the whole library
	double now = 0;
};

// Channel and Station: the whole frame (Present is the caller's).
void DrawLauncher(Theme t, const LauncherView& v);
// Flow: the background the 3D shelf draws its covers on (once per size), and the panels drawn over the covers.
void DrawFlowBackground(uint32_t* px, int w, int h);
void DrawFlowChrome(const LauncherView& v);

// The hint line every theme shows (translated, with the button symbols).
std::string LauncherHints(Theme t, bool have_games, bool network_tab);
} // namespace fe
