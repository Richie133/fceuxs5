// fceuxs5: the PS5 platform driver for FCEUX's core -- the FCEUD_* functions and globals driver.h asks every
// platform to supply.
//
// FCEUX's own drivers (drivers/Qt for Linux/macOS, drivers/win) bring a debugger, movie recording, Lua, netplay
// and video capture with them. The PS5 port keeps the emulator and leaves those tools out, so most of the hooks
// below are empty: what remains is the palette, files, time, messages and the pads.
//
// Based on the hook list of FCEUX's Qt driver (drivers/Qt/fceuWrapper.cpp, sdl-video.cpp, input.cpp).
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "fceux_driver.h"

#include "OrbisPaths.h"

#include "emufile.h"
#include "fceu.h"
#include "file.h"
#include "git.h"
#include "input.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/stat.h>

// ---- globals the core reads from its driver -------------------------------------------------------------------
int pal_emulation = 0;
int dendy = 0;
int eoptions = 0;
bool swapDuty = false;      // the Famiclone duty-cycle swap: off
// paldeemphswap: defined by vidblit.cpp
bool turbo = false;         // the core's own turbo (fast forward): the frontend does its own
int closeFinishedMovie = 0;
int KillFCEUXonFrame = 0;

namespace fceuxs5
{
uint32_t g_joy = 0;

namespace
{
uint8_t s_palette[256 * 4];
bool s_palette_dirty = true;
void (*s_sink)(const char*, bool) = nullptr;
std::mutex s_error_lock;
std::string s_last_error;

void Emit(const char* text, bool error)
{
	if (!text)
		return;
	std::string line(text);
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
		line.pop_back();
	if (line.empty())
		return;
	OrbisLog("[fceux] %s%s", error ? "error: " : "", line.c_str());
	if (error)
	{
		std::lock_guard<std::mutex> lock(s_error_lock);
		s_last_error = line;
	}
	if (s_sink)
		s_sink(line.c_str(), error);
}
} // namespace

const uint8_t* PaletteBytes()
{
	return s_palette;
}

bool TakePaletteDirty()
{
	const bool d = s_palette_dirty;
	s_palette_dirty = false;
	return d;
}

void SetMessageSink(void (*sink)(const char*, bool))
{
	s_sink = sink;
}

std::string LastError()
{
	std::lock_guard<std::mutex> lock(s_error_lock);
	return s_last_error;
}

void ClearLastError()
{
	std::lock_guard<std::mutex> lock(s_error_lock);
	s_last_error.clear();
}

void SetupInputs(int four_player_adapter)
{
	// Both ports read the same 32-bit word: the core takes player 1 and 3 from port 0's bytes, 2 and 4 from
	// port 1's (FCEUX's drivers all hand it one JSreturn this way).
	FCEUI_SetInput(0, SI_GAMEPAD, &g_joy, 0);
	FCEUI_SetInput(1, SI_GAMEPAD, &g_joy, 0);
	FCEUI_SetInputFC(four_player_adapter == 2 ? SIFC_4PLAYER : SIFC_NONE, four_player_adapter == 2 ? &g_joy : nullptr, 0);
	FCEUI_SetInputFourscore(four_player_adapter == 1);
	if (four_player_adapter == 1)
		eoptions |= 32768; // EO_FOURSCORE, read by ines.cpp
	else
		eoptions &= ~32768;
}
} // namespace fceuxs5

using fceuxs5::Emit;

// ---- palette ----------------------------------------------------------------------------------------------------
void FCEUD_SetPalette(uint8 index, uint8 r, uint8 g, uint8 b)
{
	uint8_t* p = fceuxs5::s_palette + index * 4;
	if (p[0] != r || p[1] != g || p[2] != b)
	{
		p[0] = r;
		p[1] = g;
		p[2] = b;
		p[3] = 0;
		fceuxs5::s_palette_dirty = true;
	}
}

void FCEUD_GetPalette(uint8 index, uint8* r, uint8* g, uint8* b)
{
	const uint8_t* p = fceuxs5::s_palette + index * 4;
	*r = p[0];
	*g = p[1];
	*b = p[2];
}

// ---- messages ---------------------------------------------------------------------------------------------------
void FCEUD_PrintError(const char* s)
{
	Emit(s, true);
}

void FCEUD_Message(const char* s)
{
	Emit(s, false);
}

void FCEUD_NetplayText(uint8* text)
{
	Emit(reinterpret_cast<const char*>(text), false);
}

const char* FCEUD_GetCompilerString()
{
	return "clang (ps5-payload-sdk)";
}

// ---- files ------------------------------------------------------------------------------------------------------
FILE* FCEUD_UTF8fopen(const char* fn, const char* mode)
{
	return fopen(fn, mode);
}

EMUFILE_FILE* FCEUD_UTF8_fstream(const char* n, const char* m)
{
	EMUFILE_FILE* f = new EMUFILE_FILE(n, m);
	if (f->fail())
	{
		delete f;
		return nullptr;
	}
	return f;
}

// Archives: the frontend unpacks .zip and .7z games itself before FCEUX sees them (it also patches them there:
// fe_patch.h), so the core is never handed an archive. FCEUX's own minizip path (file.cpp) still opens a .zip
// it is given directly.
ArchiveScanRecord FCEUD_ScanArchive(std::string)
{
	return ArchiveScanRecord();
}

FCEUFILE* FCEUD_OpenArchiveIndex(ArchiveScanRecord&, std::string&, int)
{
	return nullptr;
}

FCEUFILE* FCEUD_OpenArchiveIndex(ArchiveScanRecord&, std::string&, int, int*)
{
	return nullptr;
}

FCEUFILE* FCEUD_OpenArchive(ArchiveScanRecord&, std::string&, std::string*)
{
	return nullptr;
}

FCEUFILE* FCEUD_OpenArchive(ArchiveScanRecord&, std::string&, std::string*, int*)
{
	return nullptr;
}

// ---- time -------------------------------------------------------------------------------------------------------
uint64 FCEUD_GetTime(void)
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return uint64(ts.tv_sec) * 1000000ull + uint64(ts.tv_nsec / 1000);
}

uint64 FCEUD_GetTimeFreq(void)
{
	return 1000000ull;
}

void RefreshThrottleFPS()
{
	// the frontend paces the frames itself (the display's flip, or the sound for 50 Hz games)
}

// ---- input ------------------------------------------------------------------------------------------------------
void FCEUD_SetInput(bool fourscore, bool microphone, ESI port0, ESI port1, ESIFC fcexp)
{
	(void)microphone;
	(void)port0;
	(void)port1;
	fceuxs5::SetupInputs(fourscore ? 1 : (fcexp == SIFC_4PLAYER ? 2 : 0));
}

void FCEUI_UseInputPreset(int)
{
}

unsigned int* GetKeyboard(void)
{
	static unsigned int keys[256] = {};
	return keys;
}

void GetMouseData(uint32 (&md)[3])
{
	md[0] = md[1] = md[2] = 0;
}

bool FCEUD_ShouldDrawInputAids()
{
	return false;
}

// ---- netplay (not on the PS5) -----------------------------------------------------------------------------------
int FCEUD_SendData(void*, uint32)
{
	return 0;
}

int FCEUD_RecvData(void*, uint32)
{
	return 0;
}

void FCEUD_NetworkClose(void)
{
}

// ---- the desktop drivers' tools: none here ------------------------------------------------------------------------
void FCEUD_SoundToggle(void)
{
}

void FCEUD_SoundVolumeAdjust(int)
{
}

void FCEUD_SaveStateAs(void)
{
}

void FCEUD_LoadStateFrom(void)
{
}

void FCEUD_MovieRecordTo(void)
{
}

void FCEUD_MovieReplayFrom(void)
{
}

void FCEUD_AviRecordTo(void)
{
}

void FCEUD_AviStop(void)
{
}

void FCEUI_AviVideoUpdate(const unsigned char*)
{
}

bool FCEUI_AviIsRecording()
{
	return false;
}

bool FCEUI_AviEnableHUDrecording()
{
	return false;
}

bool FCEUI_AviDisableMovieMessages()
{
	return true;
}

void FCEUD_SetEmulationSpeed(int)
{
}

void FCEUD_TurboOn(void)
{
}

void FCEUD_TurboOff(void)
{
}

void FCEUD_TurboToggle(void)
{
}

int FCEUD_ShowStatusIcon(void)
{
	return 0;
}

void FCEUD_ToggleStatusIcon(void)
{
}

void FCEUD_HideMenuToggle(void)
{
}

void FCEUD_DebugBreakpoint(int)
{
}

void FCEUD_TraceInstruction(uint8*, int)
{
}

void FCEUD_FlushTrace()
{
}

void FCEUD_UpdateNTView(int, bool)
{
}

void FCEUD_UpdatePPUView(int, int)
{
}

bool FCEUD_PauseAfterPlayback()
{
	return false;
}

void FCEUD_VideoChanged()
{
	// the region changed (FCEUI_SetRegion, or a game that asks for PAL): the frontend asks the core for the frame
	// rate (FCEUI_GetDesiredFPS) and the visible lines every frame, so there is nothing to recompute here
}
