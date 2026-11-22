// fceuxs5 frontend: FCEUX's core behind a small API.
//
// FCEUX (TASEmulators/fceux, ../src) is driven through its own driver interface (driver.h): FCEUI_Emulate() runs one
// frame and hands back the indexed picture (XBuf) and the sound (48 kHz, mono); the PS5 half of the driver is
// ps5/fceux/fceux_driver.cpp. The picture goes through FCEUX's own filters (drivers/common/vidblit.cpp: hq2x, hq3x,
// Scale2x, Scale3x, Blargg's NTSC filter...) and then the CRT shaders and the scaler of ProsperoVideo.
//
// The API is the one the MIT-licensed PS5 frontend this port builds on expects (MisterTemaki's fceummPS5).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace emu
{
// The choices the settings menu offers.
struct Choice
{
	const char* name;
	const char* value; // a value of the frontend's own ("" where it isn't needed)
};
extern const Choice kAspects[]; // TV (8:7 pixels), 4:3, square pixels, 16:9
extern const int kAspectCount;
// The palettes: Mesen's (the default) and the others Mesen and Nestopia have (fe_palettes.h), then FCEUX's own.
struct Palette
{
	const char* name;
	const char* value; // "fceux" for FCEUX's built-in palette
	int extra;         // index in fe::kExtraPalettes, -1 for FCEUX's own
};
extern const Palette kPalettes[];
extern const int kPaletteCount;
// FCEUX's video filters (its "special filters", drivers/common/vidblit.cpp), before the CRT shader and the scaler.
extern const Choice kUpscalers[];
extern const int kUpscalerCount;
extern const Choice kBorders[]; // the overscan
extern const int kBorderCount;
extern const Choice kSoundQualities[]; // FCEUI_SetSoundQuality
extern const int kSoundQualityCount;
extern const Choice kRegions[]; // FCEUI_SetRegion
extern const int kRegionCount;
extern const Choice kRamStates[]; // the RAM at power-on (FCEUX's RAMInitOption)
extern const int kRamStateCount;
extern const Choice kTurboSpeeds[]; // frames between turbo presses
extern const int kTurboSpeedCount;
// The 4-player adapter (Settings, CONTROLS): Auto (the one the game is made for: Mesen's database, an NES 2.0
// header), off, the NES Four Score, the Famicom's 4-player adapter. Players 3 and 4 are on it.
extern const Choice kFourPlayers[];
extern const int kFourPlayerCount;

// The buttons of the console's pad the player can move, and the PS5 buttons they can go to.
struct PadButton
{
	const char* name; // "Square"
	unsigned bit;     // SCE_PAD_BUTTON_*; 0 = not assigned
};
extern const PadButton kPs5Buttons[]; // Cross, Circle, Square, Triangle, L1, R1, OPTIONS, touchpad, (none)
extern const int kPs5ButtonCount;
struct ConsoleButton
{
	const char* name; // "A", "Turbo B", "Disk: next side"...
	const char* key;  // its key in fceuxs5.ini: "btn_a"
	int nes_bit;      // the NES pad bit (fceuxs5::NesButton); 0x100 / 0x200 = turbo A / B; -1: the frontend's
	int default_ps5;  // index in kPs5Buttons
};
constexpr int kConsoleButtonCount = 9; // A, B, turbo A, turbo B, Start, Select, disk side, disk in/out, coin
extern const ConsoleButton kConsoleButtons[kConsoleButtonCount];

bool InitCore();
void DeinitCore();

// Starts the game. On failure, *error says why (a disk without the Famicom Disk System's BIOS says where it goes).
// The ROM hack chosen for it (fe_patch.h) is applied in memory first.
bool LoadGame(const std::string& path, std::string* error);
void CloseGame(); // writes the battery save and ends the game
bool GameLoaded();
std::string GameName();   // the file name without its extension
std::string SystemName(); // "NES", "Famicom Disk System" of the running game
std::string PatchName();  // the ROM hack applied to the running game, "" for none

// The Famicom Disk System's drive and the VS. System's coin slot (the pause menu; buttons in the settings).
bool IsDiskGame();
bool IsVsGame();
void DiskNextSide(); // ejects the disk, selects the next side, and puts it in a second later
void DiskInsertOrEject();
void InsertCoin();

// Settings from fe::Config() -> the core's options and the frontend's picture/sound settings.
void ApplySettings();

// What the 4-player adapter's Auto found for the running game: 0 none (two players), 1 the NES Four Score, 2 the
// Famicom's adapter (fe::FourPlayer); -1 when no game runs. Its name for the settings row: "2 players"...
int FourPlayerAuto();
const char* FourPlayerName(int adapter);

enum class FrameResult
{
	Continue,
	OpenMenu,     // L3 + R3 pressed together: the pause menu
	OpenSettings, // the touchpad (Settings > Touchpad opens Settings): Settings over the game
	Stopped,  // no game
};
// One frame: the pads and hot keys, FCEUI_Emulate(), the picture (and the messages) on the screen, the sound to the
// audio ring, paced by the display (60 Hz games) or by the sound (50 Hz games). Hot keys: L2 + Up / Down = save /
// load the state slot, L2 + Left / Right = change the slot, R2 held = fast forward, L2 + R2 held = rewind,
// the touchpad = Settings.
FrameResult RunFrame();

void Pause(); // nothing runs between frames; kept for the menus' symmetry
void Resume();

bool SaveState(int slot); // 1..10, /data/fceuxs5/states/<system>/<game>.fc<slot>
bool LoadState(int slot);
bool StateExists(int slot);
void Reset();
void PowerCycle();
void Osd(const std::string& text); // a message over the game for a few seconds

// Redraws the last frame into the surface (the pause menu draws over it).
void RedrawLastFrame();
} // namespace emu
