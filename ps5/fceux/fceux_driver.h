// fceuxs5: the PS5 platform driver for FCEUX's core (fceux_driver.cpp).
//
// FCEUX's core calls a set of FCEUD_* functions that every platform driver supplies (driver.h). The PS5
// frontend (frontend/fe_emu.cpp) is that driver's other half: it runs the frames, shows the picture, plays the
// sound and reads the pads. This header is what the two halves share.
//
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace fceuxs5
{
// The pads FCEUX reads (SI_GAMEPAD on both ports, the Four Score and the Famicom 4-player adapter included):
// player 1 in bits 0-7, player 2 in 8-15, player 3 in 16-23, player 4 in 24-31. Per player:
// A 0x01, B 0x02, Select 0x04, Start 0x08, Up 0x10, Down 0x20, Left 0x40, Right 0x80.
extern uint32_t g_joy;

enum NesButton : uint8_t
{
	kA = 0x01,
	kB = 0x02,
	kSelect = 0x04,
	kStart = 0x08,
	kUp = 0x10,
	kDown = 0x20,
	kLeft = 0x40,
	kRight = 0x80,
};

// The 256-entry colour table FCEUX keeps through FCEUD_SetPalette, as r, g, b, unused bytes (the layout
// SetPaletteBlitToHigh reads). TakePaletteDirty() is true once after any entry changed.
const uint8_t* PaletteBytes();
bool TakePaletteDirty();

// The core's messages ("Loaded state 3", errors). The frontend shows the newest for a few seconds.
void SetMessageSink(void (*sink)(const char* text, bool error));
// The last error FCEUX reported (FCEUD_PrintError), "" after ClearLastError().
std::string LastError();
void ClearLastError();

// Plugs the controllers in: two gamepads, plus the Four Score (adapter 1) or the Famicom 4-player adapter (2).
void SetupInputs(int four_player_adapter);
} // namespace fceuxs5
