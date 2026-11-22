// fceuxs5 frontend: settings kept in /data/fceuxs5/fceuxs5.ini (key=value lines).
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace fe
{
struct Settings
{
	// video
	int scale = 0; // ps5video::Scale: fit, integer, stretch
	int shader = 1; // ps5crt::Shader: 0 off, 1 "CRT Easymode style" (the default for every game), ...
	int aspect = 0; // emu::kAspects: TV (8:7 pixels), 4:3, square pixels, 16:9
	int palette = 0; // emu::kPalettes: Mesen's (the default), Mesen's and Nestopia's others, FCEUX's own
	int upscaler = 0; // emu::kUpscalers: FCEUX's own filters -- none, hq2x, hq3x, Scale2x, Scale3x, NTSC (Blargg), ...
	int borders = 0; // emu::kBorders: hide 8 lines top and bottom (a TV's overscan), show everything, hide all edges
	bool smooth = false; // bilinear when the scale isn't whole
	bool scanlines = false;
	bool show_fps = false;
	// audio
	bool audio = true;
	int volume = 100; // 0..100
	int sound_quality = 2; // emu::kSoundQualities: low, high, very high (the most accurate)
	bool lowpass = false; // the core's low-pass audio filter
	// emulation
	int ff_speed = 300; // % while R2 is held; 0 = as fast as possible
	bool rewind = true; // L2 + R2 rewinds
	int region = 0; // emu::kRegions: auto, NTSC, PAL, Dendy
	// 1.1: on by default -- the NES shows 8 sprites a line and games flicker the rest (SMB3's enemies and items);
	// FCEUX draws them all
	bool no_sprite_limit = true;
	bool frame_blend = false; // 1.1: flicker reduction -- each frame mixed with the one before (30 Hz flicker turns
	                          // into see-through sprites; motion gets a faint trail)
	int ram_state = 0; // emu::kRamStates: the RAM at power-on: $FF, $00, random
	bool header_db = true; // correct headers with Mesen's database (fe_nesdb.h)
	int state_slot = 1; // 1..10
	// controls
	int turbo = 2; // emu::kTurboSpeeds: frames between turbo presses
	int four_player = 0; // emu::kFourPlayers: the 4-player adapter: Auto (the game's), off, NES Four Score, Famicom's
	// the PS5 button (index in emu::kPs5Buttons) of each console button, in emu::kConsoleButtons' order:
	// A, B, turbo A, turbo B, Start, Select, disk side, disk in/out, coin. By position by default: Circle, Cross,
	// Triangle, Square, OPTIONS, touchpad, L1, R1, none.
	static constexpr int kButtons = 9;
	// 1.1: Select on L1 (the touchpad opens Settings), the disk side and the coin on R1 (a game is a disk or a VS.
	// System game, never both), the disk in / out unassigned (the next side puts it back by itself)
	static constexpr int kDefaultButtons[kButtons] = {1, 0, 3, 2, 6, 4, 5, 8, 5};
	bool touch_menu = true; // 1.1: the touchpad opens Settings over the game
	int cfg_version = 2;    // 2 = 1.1's defaults (Load moves an older fceuxs5.ini to them)
	int buttons[kButtons] = {1, 0, 3, 2, 6, 7, 4, 5, 8};
	void DefaultButtons();
	// interface (fceuxs5): the launcher's look, chosen at the first start and in Settings
	int theme = 0;             // fe::Theme: 0 Channel (list + box art), 1 Station (sky and clouds), 2 Flow (cover flow)
	bool theme_chosen = false; // false until the player picked one (the picker shows at the first start)
	bool intro = true;         // the intro video at every start (Cross / OPTIONS skips it)
	bool music = true;         // the menu music
	int music_volume = 70;     // 0..100
	bool auto_patch = true;    // ROM hacks: apply Game.ips / .ups / .bps beside Game.nes automatically (fe_patch.h)
	// library
	bool covers_download = true; // fetch box art from libretro-thumbnails
	bool debug_logs = false;     // boot.log and the others in /data/fceuxs5/logs (OrbisLogSetEnabled); off by default
	int language = 0;            // fe::Language: 0 English (the default), 1 Spanish, 2 Portuguese (Brazil), 3 French, 4 German
	int shelf_family = 0; // fe::Family the shelf shows
	int last_tab = 0; // the shelf's storage tab (fe_tabs.h): 0 Internal, 1 USB, 2 Network
	std::string last_rom; // the shelf puts the selection on this game

	void Load();
	void Save() const;
};

Settings& Config();
} // namespace fe
