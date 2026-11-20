// fceuxs5 frontend: FCEUX's core behind a small API (fe_emu.h).
//
// One RunFrame() is one FCEUI_Emulate(): the pads go into the word FCEUX reads (fceux_driver.h), the indexed picture
// comes back in XBuf and goes through FCEUX's own filter (vidblit.cpp) into a 32-bit frame, which ProsperoVideo
// scales (and a CRT shader draws, when one is chosen); the 48 kHz mono sound goes to the audio ring as stereo, the
// rate steered by the ring's fill as the MIT PS5 layer's frontend does it.
//
// Games are read here, not by FCEUX: the file (or the game inside a .zip / .7z) is read whole, its header corrected
// from Mesen's database (fe_nesdb.h), the chosen ROM hack applied (fe_patch.h), and the result written to
// /data/fceuxs5/tmp/<game name>.<ext>, which FCEUX loads. Its battery saves and states are named after that file
// (FCEUX's FileBase), so a patched game keeps saves apart from the original ("Game [Hack].sav").
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_emu.h"

#include "fceux_ps5.h"
#include "fceux_driver.h"

#include "fceu.h"
#include "cart.h"
#include "emufile.h"
#include "git.h"
#include "state.h"
#include "version.h"
#include "vidblit.h"
#include "video.h"

#include <zlib.h>

#include "ProsperoNotify.h"
#include "fe_games.h"
#include "fe_i18n.h"
#include "fe_nesdb.h"
#include "fe_palettes.h"
#include "fe_patch.h"
#include "fe_settings.h"
#include "fe_text.h"

#include "OrbisPaths.h"
#include "ProsperoAudio.h"
#include "ProsperoCrash.h"
#include "ProsperoInput.h"
#include "ProsperoSce.h"
#include "ProsperoVideo.h"

#include "netfs.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <vector>

namespace emu
{
const Choice kAspects[] = {
	{"TV (8:7 pixels)", ""},
	{"4:3", ""},
	{"Square pixels", ""},
	{"16:9", ""},
};
const int kAspectCount = int(sizeof(kAspects) / sizeof(kAspects[0]));

const Palette kPalettes[] = {
	{"Mesen", "", 0},
	{"Nestopia RGB", "", 1},
	{"Original Hardware (FirebrandX)", "", 2},
	{"PVM Style (FirebrandX)", "", 3},
	{"Unsaturated v6 (FirebrandX)", "", 4},
	{"Nostalgia (FirebrandX)", "", 5},
	{"Hybrid (Nestopia)", "", 6},
	{"NTSC (Nestopia)", "", 7},
	{"FCEUX (its own)", "fceux", -1},
};
const int kPaletteCount = int(sizeof(kPalettes) / sizeof(kPalettes[0]));

// value: FCEUX's special filter number (vidblit.cpp's specfilt), then its option (the NTSC filter's kind)
const Choice kUpscalers[] = {
	{"Off (sharp pixels)", "0,0"},
	{"hq2x", "1,0"},
	{"hq3x", "4,0"},
	{"Scale2x", "2,0"},
	{"Scale3x", "5,0"},
	{"NTSC composite (Blargg)", "3,0"},
	{"NTSC S-Video (Blargg)", "3,1"},
	{"NTSC RGB (Blargg)", "3,2"},
	{"NTSC monochrome (Blargg)", "3,3"},
	{"Prescale 2x", "6,0"},
	{"Prescale 3x", "7,0"},
	{"Prescale 4x", "8,0"},
	{"PAL blend", "9,0"},
};
const int kUpscalerCount = int(sizeof(kUpscalers) / sizeof(kUpscalers[0]));

// value: lines hidden at the top and the bottom, pixels hidden at the left and the right
const Choice kBorders[] = {
	{"Hide 8 lines top and bottom (a TV)", "8,0"},
	{"Show everything", "0,0"},
	{"Hide 8 pixels on every edge", "8,8"},
};
const int kBorderCount = int(sizeof(kBorders) / sizeof(kBorders[0]));

const Choice kSoundQualities[] = {
	{"Low", "0"},
	{"High", "1"},
	{"Very high (the most accurate)", "2"},
};
const int kSoundQualityCount = int(sizeof(kSoundQualities) / sizeof(kSoundQualities[0]));

const Choice kRegions[] = {
	{"Auto", ""},
	{"NTSC (USA, Japan)", "0"},
	{"PAL (Europe)", "1"},
	{"Dendy", "2"},
};
const int kRegionCount = int(sizeof(kRegions) / sizeof(kRegions[0]));

const Choice kRamStates[] = {
	{"FCEUX's pattern (00 00 00 00 FF FF FF FF)", "0"},
	{"$FF", "1"},
	{"$00", "2"},
	{"Random", "3"},
};
const int kRamStateCount = int(sizeof(kRamStates) / sizeof(kRamStates[0]));

const Choice kTurboSpeeds[] = {
	{"30 presses a second", "1"},
	{"20 presses a second", "2"},
	{"15 presses a second", "3"},
	{"10 presses a second", "5"},
	{"6 presses a second", "10"},
	{"4 presses a second", "15"},
	{"2 presses a second", "30"},
	{"1 press a second", "60"},
};
const int kTurboSpeedCount = int(sizeof(kTurboSpeeds) / sizeof(kTurboSpeeds[0]));

const Choice kFourPlayers[] = {
	{"Auto", ""},
	{"Off (2 players)", ""},
	{"NES Four Score", ""},
	{"Famicom 4-player adapter", ""},
};
const int kFourPlayerCount = int(sizeof(kFourPlayers) / sizeof(kFourPlayers[0]));

const PadButton kPs5Buttons[] = {
	{"Cross", SCE_PAD_BUTTON_CROSS},
	{"Circle", SCE_PAD_BUTTON_CIRCLE},
	{"Square", SCE_PAD_BUTTON_SQUARE},
	{"Triangle", SCE_PAD_BUTTON_TRIANGLE},
	{"L1", SCE_PAD_BUTTON_L1},
	{"R1", SCE_PAD_BUTTON_R1},
	{"OPTIONS", SCE_PAD_BUTTON_OPTIONS},
	{"Touchpad", SCE_PAD_BUTTON_TOUCH_PAD},
	{"None", 0},
};
const int kPs5ButtonCount = int(sizeof(kPs5Buttons) / sizeof(kPs5Buttons[0]));

const ConsoleButton kConsoleButtons[kConsoleButtonCount] = {
	{"A", "btn_a", fceuxs5::kA, 1},
	{"B", "btn_b", fceuxs5::kB, 0},
	{"Turbo A", "btn_turbo_a", 0x100, 3},
	{"Turbo B", "btn_turbo_b", 0x200, 2},
	{"Start", "btn_start", fceuxs5::kStart, 6},
	{"Select", "btn_select", fceuxs5::kSelect, 7},
	{"Disk: next side (FDS)", "btn_disk_side", -1, 4},
	{"Disk: in / out (FDS)", "btn_disk_insert", -1, 5},
	{"Insert coin (VS. System)", "btn_coin", -1, 8},
};
} // namespace emu

namespace
{
std::string Fmt(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}

constexpr int kTargetQueued = ps5audio::kRate * 60 / 1000; // 60 ms of sound
constexpr int kRewindEvery = 3;                             // frames between rewind snapshots
constexpr size_t kRewindBytes = 192u * 1024 * 1024;

double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

std::string Lower(std::string s)
{
	for (char& c : s)
		c = char(tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string ExtOf(const std::string& p)
{
	const size_t dot = p.find_last_of('.');
	const size_t slash = p.find_last_of('/');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "";
	return Lower(p.substr(dot));
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out, uint64_t max_size)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	bool ok = false;
	if (fseeko(f, 0, SEEK_END) == 0)
	{
		const off_t size = ftello(f);
		if (size >= 0 && uint64_t(size) <= max_size && fseeko(f, 0, SEEK_SET) == 0)
		{
			out.resize(size_t(size));
			ok = size == 0 || fread(out.data(), 1, out.size(), f) == out.size();
		}
	}
	fclose(f);
	return ok;
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& data)
{
	FILE* f = fopen(path.c_str(), "wb");
	if (!f)
		return false;
	bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
	ok = fclose(f) == 0 && ok;
	return ok;
}

uint64_t Fnv(const uint8_t* p, size_t n, uint64_t h = 1469598103934665603ull)
{
	for (size_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 1099511628211ull;
	return h;
}

// a name that is safe as a file name (the temp ROM, saves and states take it)
std::string SafeName(std::string s)
{
	for (char& c : s)
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
			c = '_';
	if (s.size() > 180)
		s.resize(180);
	return s;
}

struct State
{
	bool inited = false;
	bool loaded = false;
	std::string path, name, ext;  // the game; name = what saves are named after; ext with the dot
	std::string tmp_path;         // the file FCEUX loaded
	std::string patch, patch_note; // the ROM hack applied ("" none)
	fe::System system = fe::System::Nes;
	std::vector<uint8_t> rom; // what FCEUX loaded (header corrected, patched)

	// video: FCEUX's filter and the frame it made
	int filter = -1, filter_opt = -1;
	int xscale = 1, yscale = 1;
	bool filter_ntsc = false, filter_pal = false;
	std::vector<uint32_t> frame, crt_frame;
	int fw = 0, fh = 0;         // the filtered frame
	int nes_w = 256, nes_h = 224; // the console's picture it shows (before the filter)
	bool new_frame = false;
	double fps = 60.0;
	int palette_applied = -2;

	// sound
	std::vector<int16_t> in; // the frame's samples, stereo
	bool mute = false;
	double frac = 0;
	int16_t prev_l = 0, prev_r = 0;
	std::vector<int16_t> out, silence;

	// pads
	ps5input::PadState pads[ps5input::kMaxPads];
	int turbo_count[ps5input::kMaxPads][2] = {};
	uint32_t prev_p1 = 0;
	bool wait_release = true;
	int pads_applied = -1;
	fe::FourPlayer four_auto = fe::FourPlayer::None;
	std::string four_why;

	// the disk drive, the frontend's buttons
	uint64_t disk_insert_at = 0;
	uint32_t prev_extra = 0;

	// fast forward, rewind
	bool turbo = false, rewinding = false;
	std::deque<std::vector<uint8_t>> rewind;
	size_t rewind_bytes = 0;
	int rewind_tick = 0;

	// battery saves
	uint64_t sram_hash = 0;
	double next_sram_check = 0;

	// messages, FPS
	std::string osd;
	double osd_until = 0;
	bool overlay_was_drawn = false;
	int fps_frames = 0;
	double fps_since = 0, fps_shown = 0;
	uint64_t frames = 0;
	double next_stats = 0;
	int stats_reports = 0;
};
State g;

void CoreMessage(const char* text, bool error)
{
	// FCEUX's own messages over the game ("State 3 saved"...) are ours to draw; errors are shown longer
	if (!g.loaded)
		return;
	g.osd = text;
	g.osd_until = Now() + (error ? 5.0 : 2.0);
}

// ---- video ------------------------------------------------------------------------------------------------------
void ParseUpscaler(int index, int* filt, int* opt)
{
	const emu::Choice& c = emu::kUpscalers[std::max(0, std::min(index, emu::kUpscalerCount - 1))];
	*filt = atoi(c.value);
	const char* comma = strchr(c.value, ',');
	*opt = comma ? atoi(comma + 1) : 0;
}

void SetupFilter()
{
	int filt = 0, opt = 0;
	ParseUpscaler(fe::Config().upscaler, &filt, &opt);
	if (filt == g.filter && opt == g.filter_opt)
		return;
	if (g.filter >= 0)
		KillBlitToHigh();
	// 32-bit 0x00RRGGBB, as ProsperoVideo's frames are (alpha ignored)
	InitBlitToHigh(4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0, filt, opt);
	SetPaletteBlitToHigh(const_cast<uint8*>(fceuxs5::PaletteBytes()));
	g.filter = filt;
	g.filter_opt = opt;
	g.filter_ntsc = filt == 3;
	g.filter_pal = filt == 9;
	switch (filt)
	{
		case 1: case 2: case 3: case 6: g.xscale = g.yscale = 2; break;
		case 4: case 5: case 7: g.xscale = g.yscale = 3; break;
		case 8: g.xscale = g.yscale = 4; break;
		case 9: g.xscale = 3; g.yscale = 1; break;
		default: g.xscale = g.yscale = 1; break;
	}
	OrbisLog("[emu] video filter: %s", emu::kUpscalers[fe::Config().upscaler % emu::kUpscalerCount].name);
	ps5video::InvalidateFrame();
}

// XBuf (256 x 256 bytes, palette indices) -> g.frame through FCEUX's filter
void ConvertFrame(uint8* xbuf)
{
	if (!xbuf)
		return;
	if (fceuxs5::TakePaletteDirty())
		SetPaletteBlitToHigh(const_cast<uint8*>(fceuxs5::PaletteBytes()));
	int first = 0, last = 239;
	FCEUI_GetCurrentVidSystem(&first, &last);
	const int lines = std::max(1, last - first + 1);
	int side = 0;
	{
		const char* v = emu::kBorders[fe::Config().borders % emu::kBorderCount].value;
		const char* comma = strchr(v, ',');
		side = comma ? atoi(comma + 1) : 0;
	}
	if (g.filter_ntsc)
		side = 0; // Blargg's filter takes the whole line
	const int bw = 256 - 2 * side;
	const int w = g.filter_ntsc ? 2 * 301 : bw * g.xscale;
	const int h = lines * g.yscale;
	if (w != g.fw || h != g.fh)
	{
		g.fw = w;
		g.fh = h;
		g.frame.assign(size_t(w) * h, 0);
	}
	g.nes_w = bw;
	g.nes_h = lines;
	Blit8ToHigh(xbuf + first * 256 + side, reinterpret_cast<uint8*>(g.frame.data()), bw, lines, w * 4, g.xscale, g.yscale);
	g.new_frame = true;
}

double FrameAspect()
{
	const fe::Settings& cfg = fe::Config();
	const double w = g.nes_w, h = std::max(1, g.nes_h);
	switch (cfg.aspect)
	{
		case 1: return 4.0 / 3.0;
		case 2: return w / h;
		case 3: return 16.0 / 9.0;
		default: return w * 8.0 / 7.0 / h; // the NES's pixels on a TV are 8:7
	}
}

ps5video::Rect DrawLast()
{
	if (g.frame.empty())
		return ps5video::Rect{0, 0, 0, 0};
	const fe::Settings& cfg = fe::Config();
	if (cfg.shader != 0 && g.yscale > 1)
	{
		// a CRT shader draws its scanlines on the console's own lines: give it one row per line (the filter's
		// horizontal detail -- NTSC colour bleed, hq smoothing -- stays)
		const int rows = g.fh / g.yscale;
		g.crt_frame.resize(size_t(g.fw) * rows);
		for (int y = 0; y < rows; y++)
			memcpy(&g.crt_frame[size_t(y) * g.fw], &g.frame[size_t(y * g.yscale) * g.fw], size_t(g.fw) * 4);
		return ps5video::DrawFrame(g.crt_frame.data(), g.fw, rows, rows, FrameAspect(), ps5video::Scale(cfg.scale),
			cfg.smooth, cfg.scanlines, cfg.shader);
	}
	return ps5video::DrawFrame(g.frame.data(), g.fw, g.fh, g.nes_h, FrameAspect(), ps5video::Scale(cfg.scale), cfg.smooth,
		cfg.scanlines, cfg.shader);
}

void ShowOverlays(ps5video::Rect* damage)
{
	const fe::Settings& cfg = fe::Config();
	const double now = Now();
	bool drawn = false;
	if (cfg.show_fps)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%.1f fps", g.fps_shown);
		const int w = fe::TextWidth(buf, 3);
		ps5video::FillRect(ps5video::kWidth - w - 60, 20, w + 40, 50, ps5video::Rgb(0, 0, 0));
		fe::DrawText(ps5video::kWidth - w - 40, 28, buf, 3, ps5video::Rgb(255, 255, 255));
		drawn = true;
	}
	if (!g.osd.empty() && now < g.osd_until)
	{
		const std::string text = fe::FitText(g.osd, 3, ps5video::kWidth - 160);
		const int w = fe::TextWidth(text.c_str(), 3);
		ps5video::DarkenRect(40, ps5video::kHeight - 100, w + 40, 60);
		fe::DrawText(60, ps5video::kHeight - 90, text.c_str(), 3, ps5video::Rgb(255, 255, 255));
		drawn = true;
	}
	if (drawn || g.overlay_was_drawn)
		*damage = ps5video::Rect{0, 0, ps5video::kWidth, ps5video::kHeight};
	if (!drawn && g.overlay_was_drawn)
		ps5video::InvalidateFrame();
	g.overlay_was_drawn = drawn;
}

void ApplyPalette()
{
	const int p = fe::Config().palette % emu::kPaletteCount;
	if (p == g.palette_applied)
		return;
	g.palette_applied = p;
	const emu::Palette& pal = emu::kPalettes[p];
	if (pal.extra < 0 || pal.extra >= fe::kExtraPaletteCount)
	{
		FCEUI_SetUserPalette(nullptr, 0);
		return;
	}
	static uint8 rgb[64 * 3];
	for (int i = 0; i < 64; i++)
	{
		const uint32_t c = fe::kExtraPalettes[pal.extra].rgb[i];
		rgb[i * 3] = uint8(c >> 16);
		rgb[i * 3 + 1] = uint8(c >> 8);
		rgb[i * 3 + 2] = uint8(c);
	}
	FCEUI_SetUserPalette(rgb, 64);
}

// ---- sound: FCEUX's 48 kHz mono -> the ring, stereo, the rate steered by the ring's fill -------------------------
void FlushAudio()
{
	const size_t frames = g.in.size() / 2;
	if (frames == 0)
		return;
	const fe::Settings& cfg = fe::Config();
	if (!cfg.audio || g.mute || !ps5audio::Available())
	{
		g.in.clear();
		return;
	}
	int queued = ps5audio::Queued();
	if (queued < kTargetQueued / 4)
	{
		g.silence.assign(size_t(kTargetQueued - queued) * 2, 0);
		ps5audio::Push(g.silence.data(), kTargetQueued - queued);
		queued = kTargetQueued;
	}
	double adj = double(kTargetQueued - queued) / kTargetQueued;
	adj = 1.0 + std::max(-1.0, std::min(1.0, adj)) * 0.005;
	const double step = 1.0 / adj; // input frames per output frame (both 48 kHz)
	const int vol = cfg.volume * 256 / 100;
	g.out.clear();
	double pos = g.frac;
	while (pos < double(frames))
	{
		const int i = int(pos);
		const int t = int((pos - i) * 256);
		const int16_t l0 = i == 0 ? g.prev_l : g.in[size_t(i - 1) * 2];
		const int16_t r0 = i == 0 ? g.prev_r : g.in[size_t(i - 1) * 2 + 1];
		const int16_t l1 = g.in[size_t(i) * 2];
		const int16_t r1 = g.in[size_t(i) * 2 + 1];
		const int l = (l0 * (256 - t) + l1 * t) >> 8;
		const int r = (r0 * (256 - t) + r1 * t) >> 8;
		g.out.push_back(int16_t(std::max(-32768, std::min(32767, l * vol >> 8))));
		g.out.push_back(int16_t(std::max(-32768, std::min(32767, r * vol >> 8))));
		pos += step;
	}
	g.frac = pos - double(frames);
	g.prev_l = g.in[(frames - 1) * 2];
	g.prev_r = g.in[(frames - 1) * 2 + 1];
	g.in.clear();
	ps5audio::Push(g.out.data(), int(g.out.size() / 2));
}

void TakeSound(const int32* sound, int32 count)
{
	if (!sound || count <= 0 || g.mute)
		return;
	const size_t at = g.in.size();
	g.in.resize(at + size_t(count) * 2);
	for (int32 i = 0; i < count; i++)
	{
		const int32 s = std::max(-32768, std::min(32767, int(sound[i])));
		g.in[at + size_t(i) * 2] = int16_t(s);
		g.in[at + size_t(i) * 2 + 1] = int16_t(s);
	}
}

// ---- pads -------------------------------------------------------------------------------------------------------
uint32_t PadBits(int player, bool dpad_off)
{
	const ps5input::PadState& p = g.pads[player];
	if (!p.connected)
		return 0;
	const fe::Settings& cfg = fe::Config();
	uint32_t bits = 0;
	for (int i = 0; i < emu::kConsoleButtonCount; i++)
	{
		const int b = cfg.buttons[i];
		const unsigned bit = b >= 0 && b < emu::kPs5ButtonCount ? emu::kPs5Buttons[b].bit : 0;
		const int nes = emu::kConsoleButtons[i].nes_bit;
		if (bit && (p.raw_buttons & bit) && nes > 0)
			bits |= uint32_t(nes);
	}
	if (!dpad_off)
	{
		if (p.buttons & SCE_PAD_BUTTON_UP)
			bits |= fceuxs5::kUp;
		if (p.buttons & SCE_PAD_BUTTON_DOWN)
			bits |= fceuxs5::kDown;
		if (p.buttons & SCE_PAD_BUTTON_LEFT)
			bits |= fceuxs5::kLeft;
		if (p.buttons & SCE_PAD_BUTTON_RIGHT)
			bits |= fceuxs5::kRight;
	}
	return bits;
}

void UpdatePads()
{
	for (int p = 0; p < ps5input::kMaxPads; p++)
		g.pads[p] = ps5input::Snapshot(p);
	const int delay = std::max(1, atoi(emu::kTurboSpeeds[fe::Config().turbo % emu::kTurboSpeedCount].value));
	uint32_t word = 0;
	for (int p = 0; p < ps5input::kMaxPads; p++)
	{
		if (p == 0 && g.wait_release)
			continue;
		const bool l2 = p == 0 && (g.pads[0].raw_buttons & SCE_PAD_BUTTON_L2) != 0;
		uint32_t bits = PadBits(p, l2);
		// Turbo A / B: pressed on the first frame, then once every (delay + 1) frames while held
		for (int i = 0; i < 2; i++)
		{
			const uint32_t tb = i == 0 ? 0x100u : 0x200u;
			if (bits & tb)
			{
				if (g.turbo_count[p][i] == 0)
					bits |= i == 0 ? fceuxs5::kA : fceuxs5::kB;
				g.turbo_count[p][i] = (g.turbo_count[p][i] + 1) % (delay + 1);
			}
			else
				g.turbo_count[p][i] = 0;
		}
		word |= (bits & 0xff) << (8 * p);
	}
	fceuxs5::g_joy = word;
}

void ApplyControllers()
{
	int adapter = 0; // fe::FourPlayer
	switch (fe::Config().four_player % emu::kFourPlayerCount)
	{
		case 0: adapter = int(g.four_auto); break;
		case 1: adapter = 0; break;
		case 2: adapter = 1; break;
		case 3: adapter = 2; break;
	}
	if (adapter == g.pads_applied)
		return;
	g.pads_applied = adapter;
	fceuxs5::SetupInputs(adapter);
	OrbisLog("[emu] controllers: %s", emu::FourPlayerName(adapter));
}

fe::FourPlayer DetectFourPlayer(std::string* why)
{
	if (g.rom.size() > 16 && memcmp(g.rom.data(), "NES\x1a", 4) == 0)
	{
		const fe::FourPlayer h = fe::NesHeaderFourPlayer(g.rom);
		if (h != fe::FourPlayer::None)
		{
			*why = "NES 2.0 header";
			return h;
		}
		// the cartridge's PRG + CHR CRC32 (the header and a trainer left out), as fe_nesdb's table keys it
		const size_t skip = 16 + ((g.rom[6] & 4) ? 512 : 0);
		if (g.rom.size() > skip)
		{
			uLong c = crc32(0L, Z_NULL, 0);
			c = crc32(c, g.rom.data() + skip, uInt(g.rom.size() - skip));
			const char* source = "";
			const fe::FourPlayer f = fe::NesFourPlayer(uint32_t(c), &source);
			if (f != fe::FourPlayer::None)
			{
				*why = source;
				return f;
			}
		}
	}
	*why = "";
	return fe::FourPlayer::None;
}

// ---- saves ------------------------------------------------------------------------------------------------------
std::string SaveDir(const char* kind)
{
	return OrbisDir(kind) + "/" + fe::Info(g.system).folder;
}

std::string StatePath(int slot)
{
	return SaveDir("states") + "/" + g.name + ".fc" + std::to_string(slot % 10);
}

uint64_t SramHash()
{
	if (!currCartInfo || !currCartInfo->battery)
		return 0;
	uint64_t h = 1469598103934665603ull;
	for (const auto& s : currCartInfo->SaveGame)
		if (s.bufptr)
			h = Fnv(s.bufptr, s.buflen, h);
	return h;
}

void SaveSram(bool force)
{
	if (!g.loaded || !currCartInfo || !currCartInfo->battery || currCartInfo->SaveGame.empty())
		return;
	const uint64_t h = SramHash();
	if (h == g.sram_hash && !force)
		return;
	g.sram_hash = h;
	FCEU_SaveGameSave(currCartInfo);
	OrbisLog("[emu] battery save written");
}

void RewindPush()
{
	EMUFILE_MEMORY ms;
	if (!FCEUSS_SaveMS(&ms, 0)) // uncompressed: fast
		return;
	std::vector<uint8_t> s(ms.size());
	memcpy(s.data(), ms.buf(), s.size());
	g.rewind_bytes += s.size();
	g.rewind.push_back(std::move(s));
	while (g.rewind_bytes > kRewindBytes && !g.rewind.empty())
	{
		g.rewind_bytes -= g.rewind.front().size();
		g.rewind.pop_front();
	}
}

void RewindClear()
{
	g.rewind.clear();
	g.rewind_bytes = 0;
	g.rewind_tick = 0;
}

void EmulateOne(bool keep_sound, int skip)
{
	uint8* gfx = nullptr;
	int32* sound = nullptr;
	int32 ssize = 0;
	FCEUI_Emulate(&gfx, &sound, &ssize, skip);
	if (keep_sound)
		TakeSound(sound, ssize);
	if (skip == 0)
		ConvertFrame(gfx);
}
} // namespace

namespace emu
{
bool InitCore()
{
	if (g.inited)
		return true;
	FCEUXS5_STAGE(Emu, "init");
	if (!FCEUI_Initialize())
	{
		OrbisLog("[emu] FCEUI_Initialize failed");
		return false;
	}
	fceuxs5::SetMessageSink(CoreMessage);
	vidGuiMsgEna = false; // the frontend draws the messages (CoreMessage), not into the picture
	FCEUI_SetBaseDirectory(OrbisRoot());
	static std::string nv, st, bios;
	nv = OrbisDir("saves") + "/" + fe::Info(fe::System::Nes).folder;
	st = OrbisDir("states") + "/" + fe::Info(fe::System::Nes).folder;
	bios = OrbisDir("bios");
	OrbisMkdirs(nv);
	OrbisMkdirs(st);
	OrbisMkdirs(bios);
	FCEUI_SetDirOverride(FCEUIOD_NV, const_cast<char*>(nv.c_str()));
	FCEUI_SetDirOverride(FCEUIOD_STATES, const_cast<char*>(st.c_str()));
	FCEUI_SetDirOverride(FCEUIOD_FDSROM, const_cast<char*>(bios.c_str()));
	FCEUI_Sound(ps5audio::kRate);
	FCEUI_SetSoundVolume(150); // FCEUX's own default
	ApplySettings();
	SetupFilter();
	g.inited = true;
	OrbisLog("[emu] FCEUX %s ready", FCEU_VERSION_STRING);
	return true;
}

void DeinitCore()
{
	if (!g.inited)
		return;
	CloseGame();
	if (g.filter >= 0)
		KillBlitToHigh();
	g.filter = -1;
	FCEUI_Kill();
	g.inited = false;
}

void ApplySettings()
{
	const fe::Settings& cfg = fe::Config();
	FCEUI_SetSoundQuality(atoi(kSoundQualities[cfg.sound_quality % kSoundQualityCount].value));
	FCEUI_SetLowPass(cfg.lowpass ? 1 : 0);
	FCEUI_DisableSpriteLimitation(cfg.no_sprite_limit ? 1 : 0);
	RAMInitOption = atoi(kRamStates[cfg.ram_state % kRamStateCount].value);
	const int hide = atoi(kBorders[cfg.borders % kBorderCount].value);
	FCEUI_SetRenderedLines(hide, 239 - hide, hide, 239 - hide);
	if (g.loaded && cfg.region != 0)
	{
		const int want = atoi(kRegions[cfg.region % kRegionCount].value);
		if (FCEUI_GetRegion() != want)
			FCEUI_SetRegion(want, 0);
	}
	ApplyPalette();
	if (g.inited)
		SetupFilter();
	if (g.loaded)
		ApplyControllers();
	g.fps = FCEUI_GetDesiredFPS() / 16777216.0;
}

int FourPlayerAuto()
{
	return g.loaded ? int(g.four_auto) : -1;
}

const char* FourPlayerName(int adapter)
{
	switch (adapter)
	{
		case 1: return "NES Four Score";
		case 2: return "Famicom 4-player adapter";
		default: return "2 players";
	}
}

bool LoadGame(const std::string& path, std::string* error)
{
	if (!g.inited)
		return false;
	FCEUXS5_STAGE(Emu, "load game");
	CloseGame();
	const std::string file = path.substr(path.find_last_of('/') + 1);
	std::string ext = ExtOf(path);
	g.path = path;
	g.rom.clear();
	std::string inner;
	if (fe::IsArchiveExt(ext))
	{
		std::string why;
		if (!fe::ReadFromArchive(path, &inner, &g.rom, &why))
		{
			OrbisLog("[emu] could not start %s: %s", path.c_str(), why.empty() ? "no game inside the archive" : why.c_str());
			if (error)
				*error = why.empty() ? Fmt(fe::T("There is no NES or Famicom Disk System game inside %s."), file.c_str())
									 : Fmt(fe::T("fceuxs5 could not unpack %s."), file.c_str()) + " " + fe::T(why.c_str());
			netfs_end_game();
			g.path.clear();
			return false;
		}
		ext = ExtOf(inner);
	}
	else if (!ReadFile(path, g.rom, 64u << 20) || g.rom.empty())
	{
		OrbisLog("[emu] could not start %s: can't read it", path.c_str());
		if (error)
			*error = Fmt(fe::T("fceuxs5 could not read %s."), file.c_str());
		netfs_end_game();
		g.path.clear();
		return false;
	}
	if (!fe::SystemForExt(ext, &g.system))
		g.system = fe::System::Nes;
	if (ext.empty() || ext == ".zip" || ext == ".7z")
		ext = ".nes";
	g.ext = ext;

	if (g.system == fe::System::Fds && !OrbisIsFile(OrbisDir("bios") + "/disksys.rom"))
	{
		OrbisLog("[emu] could not start %s: missing the Famicom Disk System BIOS", path.c_str());
		if (error)
			*error = Fmt(fe::T("Famicom Disk System games need its BIOS: disksys.rom. Copy it to %s/ and try again."),
				OrbisDir("bios").c_str());
		netfs_end_game();
		g.path.clear();
		g.rom.clear();
		return false;
	}

	// the ROM hack first (it is made for the dump as it is), then the header correction
	std::string base = file.substr(0, file.size() - ExtOf(file).size());
	g.patch = fe::PatchForGame(path);
	g.patch_note.clear();
	if (!g.patch.empty())
	{
		std::string perr;
		if (fe::ApplyPatch(g.patch, g.rom, &g.patch_note, &perr))
		{
			OrbisLog("[emu] ROM hack applied: %s%s%s", g.patch.c_str(), g.patch_note.empty() ? "" : " -- ",
				g.patch_note.c_str());
			base += " [" + fe::PatchLabel(g.patch) + "]";
		}
		else
		{
			OrbisLog("[emu] ROM hack %s not applied: %s", g.patch.c_str(), perr.c_str());
			ProsperoNotify("fceuxs5: %s", (fe::PatchLabel(g.patch) + " - " + perr).c_str());
			g.patch.clear();
		}
	}
	std::string fixed;
	if (fe::Config().header_db && ext == ".nes" && fe::FixNesHeader(g.rom, &fixed))
		OrbisLog("[emu] header corrected from Mesen's database: %s", fixed.c_str());

	g.name = SafeName(base);
	OrbisMkdirs(OrbisDir("tmp"));
	g.tmp_path = OrbisDir("tmp") + "/" + g.name + ext;
	if (!WriteFile(g.tmp_path, g.rom))
	{
		OrbisLog("[emu] could not write %s", g.tmp_path.c_str());
		if (error)
			*error = Fmt(fe::T("fceuxs5 could not write its working copy of %s (is the drive full?)."), file.c_str());
		netfs_end_game();
		g.path.clear();
		g.rom.clear();
		return false;
	}

	// saves per system (a disk and a cartridge may share a name)
	static std::string nv, st;
	nv = SaveDir("saves");
	st = SaveDir("states");
	OrbisMkdirs(nv);
	OrbisMkdirs(st);
	FCEUI_SetDirOverride(FCEUIOD_NV, const_cast<char*>(nv.c_str()));
	FCEUI_SetDirOverride(FCEUIOD_STATES, const_cast<char*>(st.c_str()));

	OrbisLog("[emu] loading %s%s%s as %s", path.c_str(), inner.empty() ? "" : " -> ", inner.c_str(), g.tmp_path.c_str());
	ApplySettings();
	g.frame.clear();
	g.fw = g.fh = 0;
	g.in.clear();
	g.mute = false;
	fceuxs5::ClearLastError();
	if (!FCEUI_LoadGame(g.tmp_path.c_str(), 1, true))
	{
		netfs_end_game();
		const std::string why = fceuxs5::LastError();
		OrbisLog("[emu] could not start %s: %s", path.c_str(), why.c_str());
		if (error)
			*error = Fmt(fe::T("FCEUX could not load %s (a board it doesn't know, or a damaged file)."), file.c_str()) +
				(why.empty() ? "" : " " + why);
		unlink(g.tmp_path.c_str());
		g.path.clear();
		g.rom.clear();
		return false;
	}
	if (fe::Config().region != 0)
		FCEUI_SetRegion(atoi(kRegions[fe::Config().region % kRegionCount].value), 0);
	g.loaded = true;
	g.four_auto = DetectFourPlayer(&g.four_why);
	g.pads_applied = -1;
	ApplyControllers();
	g.fps = FCEUI_GetDesiredFPS() / 16777216.0;
	g.wait_release = true;
	g.prev_p1 = 0;
	g.turbo = g.rewinding = false;
	g.frac = 0;
	g.prev_l = g.prev_r = 0;
	g.frames = 0;
	g.fps_frames = 0;
	g.fps_since = Now();
	g.next_stats = Now() + 1.0;
	g.stats_reports = 0;
	g.next_sram_check = Now() + 3.0;
	g.sram_hash = SramHash();
	g.overlay_was_drawn = false;
	g.disk_insert_at = 0;
	g.prev_extra = 0;
	RewindClear();
	ps5video::InvalidateFrame();
	netfs_set_realtime(true);
	if (!g.patch.empty())
		Osd(g.patch_note.empty() ? Fmt(fe::T("ROM hack: %s"), fe::PatchLabel(g.patch).c_str()) : g.patch_note);
	OrbisLog("[emu] running \"%s\" (%s), %.3f fps, mapper %d", g.name.c_str(), SystemName().c_str(), g.fps,
		GameInfo ? int(GameInfo->type) : -1);
	return true;
}

void CloseGame()
{
	if (!g.loaded)
		return;
	FCEUXS5_STAGE(Emu, "close game");
	SaveSram(false);
	FCEUI_CloseGame(); // writes the battery save (and a disk's changes) once more
	g.loaded = false;
	g.frame.clear();
	g.rom.clear();
	RewindClear();
	if (!g.tmp_path.empty())
		unlink(g.tmp_path.c_str());
	g.tmp_path.clear();
	netfs_set_realtime(false);
	netfs_end_game();
	OrbisLog("[emu] game closed");
}

bool GameLoaded()
{
	return g.loaded;
}

std::string GameName()
{
	return g.name;
}

std::string SystemName()
{
	return g.loaded || !g.path.empty() ? fe::Info(g.system).name : "";
}

std::string PatchName()
{
	return g.patch.empty() ? "" : fe::PatchLabel(g.patch);
}

bool IsDiskGame()
{
	return g.loaded && GameInfo && GameInfo->type == GIT_FDS;
}

bool IsVsGame()
{
	return g.loaded && GameInfo && GameInfo->type == GIT_VSUNI;
}

void DiskNextSide()
{
	if (!IsDiskGame())
		return;
	// as on the console: the disk out, turned over, and in again a second later
	FCEUI_FDSInsert(); // out
	FCEUI_FDSSelect();
	g.disk_insert_at = g.frames + 60;
	OrbisLog("[emu] disk: next side");
}

void DiskInsertOrEject()
{
	if (!IsDiskGame())
		return;
	g.disk_insert_at = 0;
	FCEUI_FDSInsert();
	OrbisLog("[emu] disk: in / out");
}

void InsertCoin()
{
	if (!IsVsGame())
		return;
	FCEUI_VSUniCoin();
	Osd("Coin inserted");
}

void Pause() {}

void Resume()
{
	ApplySettings();
	g.wait_release = true;
	ps5video::InvalidateFrame();
	g.new_frame = true;
}

void Osd(const std::string& text)
{
	g.osd = fe::T(text.c_str());
	g.osd_until = Now() + 2.5;
}

bool SaveState(int slot)
{
	if (!g.loaded)
		return false;
	OrbisMkdirs(SaveDir("states"));
	const std::string p = StatePath(slot);
	EMUFILE_MEMORY ms;
	if (!FCEUSS_SaveMS(&ms, 6))
	{
		Osd(Fmt(fe::T("Could not save state %d"), slot));
		return false;
	}
	std::vector<uint8_t> data(ms.size());
	memcpy(data.data(), ms.buf(), data.size());
	const std::string tmp = p + ".tmp";
	const bool ok = WriteFile(tmp, data) && rename(tmp.c_str(), p.c_str()) == 0;
	Osd(Fmt(fe::T(ok ? "State %d saved" : "Could not save state %d"), slot));
	OrbisLog("[emu] state %d %s: %s (%zu bytes)", slot, ok ? "saved" : "NOT saved", p.c_str(), data.size());
	return ok;
}

bool LoadState(int slot)
{
	if (!g.loaded)
		return false;
	std::vector<uint8_t> data;
	if (!ReadFile(StatePath(slot), data, 64u << 20) || data.empty())
	{
		Osd(Fmt(fe::T("State %d is empty"), slot));
		return false;
	}
	EMUFILE_MEMORY ms(&data);
	const bool ok = FCEUSS_LoadFP(&ms, SSLOADPARAM_NOBACKUP);
	Osd(Fmt(fe::T(ok ? "State %d loaded" : "Could not load state %d"), slot));
	if (ok)
		RewindClear();
	return ok;
}

bool StateExists(int slot)
{
	return g.loaded && OrbisIsFile(StatePath(slot));
}

void Reset()
{
	if (!g.loaded)
		return;
	FCEUI_ResetNES();
	RewindClear();
	Osd("Reset");
}

void PowerCycle()
{
	if (!g.loaded)
		return;
	FCEUI_PowerNES();
	RewindClear();
	Osd("Power cycle");
}

void RedrawLastFrame()
{
	DrawLast();
}

FrameResult RunFrame()
{
	if (!g.loaded)
		return FrameResult::Stopped;
	fe::Settings& cfg = fe::Config();

	// -- hot keys (player 1)
	ps5input::Poll();
	const uint32_t raw = ps5input::Pad(0).raw_buttons;
	if (g.wait_release)
	{
		g.wait_release = (raw & (SCE_PAD_BUTTON_L3 | SCE_PAD_BUTTON_R3 | SCE_PAD_BUTTON_CROSS | SCE_PAD_BUTTON_CIRCLE)) != 0;
		g.prev_p1 = raw;
	}
	const uint32_t pressed = raw & ~g.prev_p1;
	g.prev_p1 = raw;
	const uint32_t menu_combo = SCE_PAD_BUTTON_L3 | SCE_PAD_BUTTON_R3;
	if ((raw & menu_combo) == menu_combo && (pressed & menu_combo))
		return FrameResult::OpenMenu;

	const bool l2 = (raw & SCE_PAD_BUTTON_L2) != 0;
	const bool r2 = (raw & SCE_PAD_BUTTON_R2) != 0;
	if (l2 && !r2)
	{
		if (pressed & SCE_PAD_BUTTON_UP)
			SaveState(cfg.state_slot);
		else if (pressed & SCE_PAD_BUTTON_DOWN)
			LoadState(cfg.state_slot);
		else if (pressed & (SCE_PAD_BUTTON_LEFT | SCE_PAD_BUTTON_RIGHT))
		{
			cfg.state_slot = (pressed & SCE_PAD_BUTTON_RIGHT) ? cfg.state_slot % 10 + 1 : (cfg.state_slot + 8) % 10 + 1;
			Osd(Fmt(fe::T(StateExists(cfg.state_slot) ? "State slot %d (used)" : "State slot %d (empty)"), cfg.state_slot));
			cfg.Save();
		}
	}
	if (!g.wait_release)
	{
		uint32_t extra = 0;
		for (int i = 0; i < kConsoleButtonCount; i++)
		{
			const int b = cfg.buttons[i];
			const unsigned bit = b >= 0 && b < kPs5ButtonCount ? kPs5Buttons[b].bit : 0;
			if (kConsoleButtons[i].nes_bit < 0 && bit && (raw & bit))
				extra |= 1u << i;
		}
		const uint32_t newly = extra & ~g.prev_extra;
		g.prev_extra = extra;
		if (newly & (1u << 6))
			DiskNextSide();
		if (newly & (1u << 7))
			DiskInsertOrEject();
		if (newly & (1u << 8))
			InsertCoin();
	}
	g.rewinding = cfg.rewind && l2 && r2;
	g.turbo = r2 && !l2;

	// -- emulate
	FCEUXS5_STAGE(Emu, "FCEUI_Emulate");
	UpdatePads();
	if (g.rewinding)
	{
		if (!g.rewind.empty())
		{
			std::vector<uint8_t>& s = g.rewind.back();
			EMUFILE_MEMORY ms(&s);
			FCEUSS_LoadFP(&ms, SSLOADPARAM_NOBACKUP);
			g.rewind_bytes -= s.size();
			g.rewind.pop_back();
		}
		g.mute = true;
		EmulateOne(false, 0);
		g.mute = false;
		g.in.clear();
	}
	else
	{
		const int runs = g.turbo ? (cfg.ff_speed == 0 ? 8 : std::max(2, (cfg.ff_speed + 50) / 100)) : 1;
		for (int i = 0; i < runs; i++)
		{
			g.mute = g.turbo;
			EmulateOne(!g.turbo, i + 1 < runs ? 1 : 0);
			g.frames++;
			g.fps_frames++;
			if (cfg.rewind && ++g.rewind_tick >= kRewindEvery)
			{
				g.rewind_tick = 0;
				RewindPush();
			}
		}
		g.mute = false;
	}
	if (g.disk_insert_at && g.frames >= g.disk_insert_at)
	{
		g.disk_insert_at = 0;
		FCEUI_FDSInsert(); // the side selected a second ago goes in
	}
	FlushAudio();

	// -- picture
	const double now = Now();
	if (now - g.fps_since >= 1.0)
	{
		g.fps_shown = g.fps_frames / (now - g.fps_since);
		g.fps_frames = 0;
		g.fps_since = now;
	}
	g.fps = FCEUI_GetDesiredFPS() / 16777216.0;
	const bool pal = g.fps < 55.0;
	if (g.new_frame || (!g.osd.empty() && now < g.osd_until + 0.1) || cfg.show_fps)
	{
		g.new_frame = false;
		ps5video::Rect r = DrawLast();
		ShowOverlays(&r);
		ps5video::Present(r.x, r.y, r.w, r.h, !pal && !g.turbo);
	}

	// -- pacing on the sound: 50 Hz games, or a ring filling up
	if (!g.turbo && !g.rewinding)
	{
		if (cfg.audio && ps5audio::Available())
		{
			const int limit = pal ? kTargetQueued : kTargetQueued * 2;
			const double give_up = Now() + 0.1;
			while (ps5audio::Queued() > limit && Now() < give_up)
				usleep(1000);
		}
		else if (pal)
		{
			static double next = 0;
			if (next < now - 0.1)
				next = now;
			next += 1.0 / g.fps;
			if (next > Now())
				usleep(useconds_t((next - Now()) * 1e6));
		}
	}

	// -- battery save, every 3 s when it changed; the log, now and then
	if (now >= g.next_sram_check)
	{
		g.next_sram_check = now + 3.0;
		SaveSram(false);
	}
	if (now >= g.next_stats)
	{
		g.stats_reports++;
		g.next_stats = now + (g.stats_reports < 3 ? 5.0 : 60.0);
		OrbisLog("[emu] frame %llu, %.1f fps, audio queued %d (%.0f ms), underruns %llu, shader %.1f ms",
			(unsigned long long)g.frames, g.fps_shown, ps5audio::Queued(), ps5audio::Queued() * 1000.0 / ps5audio::kRate,
			(unsigned long long)ps5audio::Underruns(), ps5video::TakeShaderMs());
	}
	return FrameResult::Continue;
}
} // namespace emu
