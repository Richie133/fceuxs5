// fceuxs5 frontend: the three interfaces (fe_theme.h).
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_theme.h"

#include "fe_draw.h"
#include "fe_i18n.h"
#include "fe_media.h"
#include "fe_settings.h"
#include "fe_text.h"
#include "fe_ui.h"

#include "OrbisPaths.h"
#include "ProsperoInput.h"
#include "ProsperoSce.h"
#include "ProsperoVideo.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>

#ifndef FCEUXS5_VERSION
#define FCEUXS5_VERSION "dev"
#endif

namespace fe
{
namespace
{
using ps5video::Rgb;
constexpr int W = ps5video::kWidth;
constexpr int H = ps5video::kHeight;

std::string Upper(std::string s)
{
	for (char& c : s)
		c = char(toupper(static_cast<unsigned char>(c)));
	return s;
}

// text with a dark (or any) outline all around, for titles over pictures
void OutlinedText(int x, int y, const char* s, int px, uint32_t color, uint32_t outline, int thick)
{
	for (int dy = -thick; dy <= thick; dy++)
		for (int dx = -thick; dx <= thick; dx++)
			if (dx * dx + dy * dy <= thick * thick + 1 && (dx || dy))
				DrawTextPx(x + dx, y + dy, s, px, outline, false);
	DrawTextPx(x, y, s, px, color, false);
}

void CenterPx(int cx, int y, const std::string& s, int px, uint32_t color, bool shadow = false)
{
	DrawTextPx(cx - TextWidthPx(s.c_str(), px) / 2, y, s.c_str(), px, color, shadow);
}

const CoverTex* CoverAt(const LauncherView& v, int i)
{
	if (!v.covers || i < 0 || i >= int(v.covers->size()))
		return nullptr;
	const CoverPtr& c = (*v.covers)[size_t(i)];
	return c ? c.get() : nullptr;
}

// box art, or a card with the title when there is none (yet)
void CoverOrCard(const draw::Target& t, const LauncherView& v, int i, int x, int y, int w, int h, uint32_t card_top,
	uint32_t card_bottom, uint32_t text)
{
	const CoverTex* tex = CoverAt(v, i);
	if (tex && tex->real)
	{
		int ox, oy, ow, oh;
		draw::FitRect(*tex, x, y, w, h, &ox, &oy, &ow, &oh);
		draw::Fill(t, ox + 10, oy + 12, ow, oh, Rgb(0, 0, 0), 60); // shadow
		draw::Cover(t, *tex, ox, oy, ow, oh);
		return;
	}
	const int cw = std::min(w, int(h * 0.72)), ch = std::min(h, int(cw / 0.72));
	const int cx = x + (w - cw) / 2, cy = y + (h - ch) / 2;
	draw::RoundRect(t, cx, cy, cw, ch, 18, card_top, card_bottom);
	draw::RoundRectOutline(t, cx, cy, cw, ch, 18, 4, draw::Shade(card_bottom, 70));
	// a pad on the card: an NES controller in two greys and red
	const int pw = cw * 6 / 10, ph = pw * 4 / 10, px0 = cx + (cw - pw) / 2, py0 = cy + ch / 4;
	draw::RoundRect(t, px0, py0, pw, ph, 10, Rgb(70, 70, 76), Rgb(50, 50, 56));
	draw::Fill(t, px0 + pw / 8, py0 + ph * 2 / 5, pw / 6, ph / 6, Rgb(20, 20, 22));
	draw::Fill(t, px0 + pw / 8 + pw / 12 - ph / 12, py0 + ph / 4, ph / 6, ph / 2, Rgb(20, 20, 22));
	draw::Circle(t, px0 + pw * 7 / 10, py0 + ph / 2, ph / 8, Rgb(200, 40, 40));
	draw::Circle(t, px0 + pw * 17 / 20, py0 + ph / 2, ph / 8, Rgb(200, 40, 40));
	if (v.games && i >= 0 && i < int(v.games->size()))
	{
		const std::string title = FitText((*v.games)[size_t(i)].title, 3, cw - 40);
		const int px = std::max(22, std::min(40, cw / 12));
		const std::string fit = FitText((*v.games)[size_t(i)].title, 3, cw - 30);
		(void)title;
		CenterPx(cx + cw / 2, cy + ch * 2 / 3, fit, px, text);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Channel
// ---------------------------------------------------------------------------------------------------------------
const uint32_t kChRed = Rgb(214, 56, 62);
const uint32_t kChText = Rgb(46, 46, 54);

void ChannelBackground(const draw::Target& t)
{
	draw::VGradient(t, 0, 0, W, 150, Rgb(88, 88, 96), Rgb(150, 150, 158));
	draw::Fill(t, 0, 148, W, 4, Rgb(235, 235, 240));
	draw::VGradient(t, 0, 152, W, H - 152, Rgb(206, 206, 212), Rgb(238, 238, 242));
	// faint stripes, as a console channel's backdrop has
	for (int y = 152; y < H; y += 6)
		draw::Fill(t, 0, y, W, 2, Rgb(255, 255, 255), 28);
	// the two panels' frames
	draw::RoundRect(t, 96, 206, 1000, 640, 30, Rgb(0, 0, 0), Rgb(0, 0, 0), 40); // shadow
	draw::RoundRect(t, 88, 196, 1000, 640, 30, Rgb(255, 255, 255), Rgb(246, 246, 248));
	draw::RoundRectOutline(t, 88, 196, 1000, 640, 30, 6, kChRed);
	draw::RoundRect(t, 1148, 206, 690, 640, 30, Rgb(0, 0, 0), Rgb(0, 0, 0), 40);
	draw::RoundRect(t, 1140, 196, 690, 640, 30, Rgb(255, 255, 255), Rgb(244, 244, 246));
	draw::RoundRectOutline(t, 1140, 196, 690, 640, 30, 6, kChRed);
}

void Pill(const draw::Target& t, int x, int y, int w, int h, const std::string& label, bool gear)
{
	draw::RoundRect(t, x + 4, y + 8, w, h, h / 2, Rgb(0, 0, 0), Rgb(0, 0, 0), 50);
	draw::RoundRect(t, x, y, w, h, h / 2, Rgb(255, 255, 255), Rgb(222, 222, 228));
	draw::RoundRectOutline(t, x, y, w, h, h / 2, 4, Rgb(150, 150, 160));
	const int ix = x + h * 3 / 4, iy = y + h / 2;
	if (gear)
	{
		for (int k = 0; k < 8; k++)
		{
			const double a = k * M_PI / 4;
			draw::Circle(t, ix + int(std::cos(a) * h * 0.24), iy + int(std::sin(a) * h * 0.24), h / 12, Rgb(70, 70, 78));
		}
		draw::Circle(t, ix, iy, h / 4, Rgb(70, 70, 78));
		draw::Circle(t, ix, iy, h / 10, Rgb(240, 240, 244));
	}
	else
	{
		// a back arrow
		draw::Triangle(t, ix - h / 5, iy - h / 20, ix, iy - h / 4, ix + h / 5, iy - h / 20, Rgb(70, 70, 78));
		draw::Fill(t, ix - h / 14, iy - h / 20, h / 7, h / 4, Rgb(70, 70, 78));
	}
	DrawTextPx(x + h * 3 / 2, y + h / 2 - 24, label.c_str(), 46, kChText);
}

void DrawChannel(const LauncherView& v)
{
	static draw::Canvas bg;
	if (bg.key != int(Theme::Channel))
	{
		ChannelBackground(bg.target());
		bg.key = int(Theme::Channel);
	}
	bg.CopyToScreen();
	const draw::Target t = draw::Screen();
	DrawTextPx(92, 44, T("Choose Game"), 62, Rgb(255, 255, 255), true);
	// the wordmark, top right: "fceux" white, "s5" red, outlined
	const int wx = W - 90 - TextWidthPx("fceuxs5", 76);
	OutlinedText(wx, 40, "fceux", 76, Rgb(255, 255, 255), Rgb(30, 30, 34), 4);
	OutlinedText(wx + TextWidthPx("fceux", 76), 40, "s5", 76, Rgb(236, 60, 60), Rgb(30, 30, 34), 4);
	DrawTextPx(W - 90 - TextWidthPx(FCEUXS5_VERSION, 26), 14, FCEUXS5_VERSION, 26, Rgb(235, 235, 240));

	const int n = v.games ? int(v.games->size()) : 0;
	// -- the list
	const int lx = 88, ly = 196, lw = 1000, lh = 640;
	const int row_h = 64, top = ly + 28, rows = (lh - 56) / row_h;
	if (n > 0)
	{
		int first = std::clamp(int(std::lround(v.pos)) - rows / 2, 0, std::max(0, n - rows));
		for (int r = 0; r < rows && first + r < n; r++)
		{
			const int i = first + r;
			const int y = top + r * row_h;
			if (i == v.sel)
			{
				draw::RoundRect(t, lx + 20, y, lw - 120, row_h - 4, 14, Rgb(255, 222, 226), Rgb(246, 168, 176));
				draw::RoundRectOutline(t, lx + 20, y, lw - 120, row_h - 4, 14, 2, Rgb(220, 120, 130));
			}
			const std::string title = FitText((*v.games)[size_t(i)].title, 4, lw - 180);
			DrawTextPx(lx + 40, y + 9, title.c_str(), 42, i == v.sel ? Rgb(30, 30, 36) : kChText);
		}
		// scroll bar: arrows and a thumb on a red track
		const int sx = lx + lw - 80, sy = ly + 24, sh = lh - 48;
		draw::RoundRect(t, sx + 8, sy + 70, 44, sh - 140, 12, Rgb(232, 86, 92), Rgb(196, 40, 48));
		for (int k = 0; k < 2; k++)
		{
			const int by = k == 0 ? sy : sy + sh - 62;
			draw::RoundRect(t, sx, by, 60, 62, 12, Rgb(255, 255, 255), Rgb(226, 226, 230));
			draw::RoundRectOutline(t, sx, by, 60, 62, 12, 3, Rgb(150, 150, 158));
			if (k == 0)
				draw::Triangle(t, sx + 30, by + 16, sx + 14, by + 44, sx + 46, by + 44, Rgb(40, 40, 46));
			else
				draw::Triangle(t, sx + 14, by + 18, sx + 46, by + 18, sx + 30, by + 46, Rgb(40, 40, 46));
		}
		const int track = sh - 140;
		const int thumb = std::max(60, track * std::min(rows, n) / std::max(1, n));
		const int ty = sy + 70 + (n > 1 ? (track - thumb) * v.sel / (n - 1) : 0);
		draw::RoundRect(t, sx + 2, ty, 56, thumb, 12, Rgb(255, 255, 255), Rgb(228, 228, 232));
		draw::RoundRectOutline(t, sx + 2, ty, 56, thumb, 12, 3, Rgb(150, 150, 158));
		// -- the preview
		const GameInfo& g = (*v.games)[size_t(v.sel)];
		CoverOrCard(t, v, v.sel, 1180, 226, 610, 500, Rgb(236, 236, 240), Rgb(206, 206, 212), kChText);
		std::string info = std::string(Info(g.system).name);
		if (!g.region.empty())
			info += "  \xC2\xB7  " + g.region;
		char count[48];
		snprintf(count, sizeof count, "  \xC2\xB7  %d / %d", v.sel + 1, n);
		info += count;
		CenterPx(1140 + 345, 744, FitText(g.title, 3, 640), 34, kChText);
		CenterPx(1140 + 345, 790, FitText(info, 2, 640), 26, Rgb(110, 110, 120));
	}
	else
	{
		int y = ly + 60;
		for (const std::string& line : v.empty_lines)
		{
			DrawTextPx(lx + 50, y, FitText(line, 3, lw - 100).c_str(), y == ly + 60 ? 44 : 30, kChText);
			y += y == ly + 60 ? 80 : 48;
		}
		CoverOrCard(t, v, -1, 1180, 226, 610, 560, Rgb(236, 236, 240), Rgb(206, 206, 212), kChText);
	}
	// storage tab and status above the panels
	DrawTextPx(96, 160, FitText(v.storage + "  \xC2\xB7  " + v.family, 2, 900).c_str(), 28, Rgb(70, 70, 80));
	if (!v.status.empty())
		DrawTextPx(W - 90 - TextWidthPx(v.status.c_str(), 26), 162, v.status.c_str(), 26, Rgb(70, 70, 80));
	if (!v.patch.empty())
		DrawTextPx(1160, 160, FitText(std::string(T("ROM hack: ")) + v.patch, 2, 600).c_str(), 28, kChRed);
	// buttons
	Pill(t, 230, 880, 600, 116, std::string(icon::Triangle) + "  " + T("Settings"), true);
	Pill(t, 1090, 880, 600, 116, std::string(icon::Options) + "  " + T("Exit"), false);
	const std::string hints = LauncherHints(Theme::Channel, n > 0, false);
	CenterPx(W / 2, 1030, FitText(hints, 2, W - 80), 26, Rgb(80, 80, 90));
}

// ---------------------------------------------------------------------------------------------------------------
// Station
// ---------------------------------------------------------------------------------------------------------------
void Cloud(const draw::Target& t, int x, int y, int s)
{
	const uint32_t white = Rgb(255, 255, 255);
	draw::SoftEllipse(t, x, y + s / 3, s * 2, s * 2 / 3, Rgb(200, 225, 250), 120);
	draw::SoftEllipse(t, x - s, y + s / 6, s, s * 2 / 3, white, 235);
	draw::SoftEllipse(t, x, y - s / 6, s * 6 / 5, s, white, 245);
	draw::SoftEllipse(t, x + s, y + s / 6, s, s * 3 / 4, white, 235);
}

void StationBackground(const draw::Target& t)
{
	draw::VGradient(t, 0, 0, W, 900, Rgb(56, 140, 228), Rgb(178, 218, 252));
	Cloud(t, 260, 150, 70);
	Cloud(t, 1640, 120, 80);
	Cloud(t, 1180, 300, 55);
	Cloud(t, 520, 420, 45);
	Cloud(t, 1760, 520, 50);
	// rolling hills, far (blue-green) and near (green)
	for (int k = 0; k < 7; k++)
	{
		const int cx = -100 + k * 340, r = 260 + (k % 3) * 70;
		draw::Circle(t, cx, 930, r, Rgb(140, 196, 186));
	}
	for (int k = 0; k < 6; k++)
	{
		const int cx = 80 + k * 380, r = 180 + (k % 2) * 60;
		draw::Circle(t, cx, 960, r, Rgb(72, 176, 92));
		draw::SoftEllipse(t, cx - r / 3, 960 - r * 2 / 3, r / 3, r / 6, Rgb(160, 230, 160), 120); // shine
	}
	// the ground: a grass edge of round bumps over earth with pebbles
	draw::VGradient(t, 0, 950, W, H - 950, Rgb(214, 150, 84), Rgb(170, 108, 56));
	for (int x = 0; x < W + 40; x += 44)
		draw::Circle(t, x, 952, 26, Rgb(56, 180, 70));
	draw::Fill(t, 0, 940, W, 18, Rgb(56, 180, 70));
	unsigned seed = 1234567;
	for (int k = 0; k < 160; k++)
	{
		seed = seed * 1103515245u + 12345u;
		const int x = int(seed % W);
		seed = seed * 1103515245u + 12345u;
		const int y = 990 + int(seed % 80);
		draw::Circle(t, x, y, 3 + int(seed % 4), Rgb(150, 92, 44), 200);
	}
}

const uint32_t kTitleColors[] = {Rgb(64, 196, 64), Rgb(56, 116, 236), Rgb(232, 56, 48), Rgb(250, 206, 40)};

void StationTitle(int cy)
{
	const char* title = "FCEUXS5";
	const int px = 150;
	const int w = TextWidthPx(title, px);
	int x = (W - w) / 2;
	char one[2] = {0, 0};
	for (int i = 0; title[i]; i++)
	{
		one[0] = title[i];
		DrawTextPx(x + 8, cy + 10, one, px, Rgb(20, 30, 70), false); // shadow
		OutlinedText(x, cy, one, px, kTitleColors[i % 4], Rgb(20, 24, 50), 6);
		x += TextWidthPx(one, px);
	}
	const char* sub = "NES EMULATOR FOR THE PS5";
	const int sw = TextWidthPx(sub, 44);
	OutlinedText((W - sw) / 2, cy + 156, sub, 44, Rgb(236, 72, 40), Rgb(255, 255, 255), 3);
}

void DrawStation(const LauncherView& v)
{
	static draw::Canvas bg;
	if (bg.key != int(Theme::Station))
	{
		StationBackground(bg.target());
		bg.key = int(Theme::Station);
	}
	bg.CopyToScreen();
	const draw::Target t = draw::Screen();
	StationTitle(8);
	DrawTextPx(60, 40, FCEUXS5_VERSION, 30, Rgb(255, 255, 255), true);
	DrawTextPx(60, 78, "FCEUX 2.8", 26, Rgb(255, 255, 255), true);
	const char* by = "BY RICHIE133";
	DrawTextPx(W - 60 - TextWidthPx(by, 30), 40, by, 30, Rgb(255, 255, 255), true);
	const std::string tab = Upper(v.storage);
	DrawTextPx(W - 60 - TextWidthPx(tab.c_str(), 26), 80, tab.c_str(), 26, Rgb(255, 255, 255), true);

	// the list, in a translucent rounded box
	const int bx = 400, by0 = 250, bw = 1120, bh = 650;
	draw::RoundRect(t, bx, by0, bw, bh, 44, Rgb(150, 196, 246), Rgb(110, 166, 236), 120);
	draw::RoundRectOutline(t, bx, by0, bw, bh, 44, 6, Rgb(36, 84, 196));
	const int n = v.games ? int(v.games->size()) : 0;
	const int row_h = 52, rows = (bh - 60) / row_h, top = by0 + 32;
	if (n > 0)
	{
		const int first = std::clamp(int(std::lround(v.pos)) - rows / 2, 0, std::max(0, n - rows));
		for (int r = 0; r < rows && first + r < n; r++)
		{
			const int i = first + r;
			const std::string name = FitText(Upper((*v.games)[size_t(i)].title), 4, bw - 140);
			const int y = top + r * row_h;
			if (i == v.sel)
			{
				// a slow glow behind the selection
				const int glow = 70 + int(40 * std::sin(v.now * 4.0));
				draw::RoundRect(t, bx + 30, y - 4, bw - 60, row_h, 20, Rgb(255, 255, 255), Rgb(220, 236, 255), glow);
				DrawTextPx(bx + 60, y, name.c_str(), 40, Rgb(255, 255, 255), true);
			}
			else
				DrawTextPx(bx + 60, y, name.c_str(), 40, Rgb(255, 196, 72), true);
		}
		// the selected game's box art, framed, on the right
		const int cx = 1560, cy = 300, cw = 320, chh = 440;
		draw::RoundRect(t, cx - 12, cy - 12, cw + 24, chh + 24, 18, Rgb(255, 255, 255), Rgb(230, 238, 255));
		draw::RoundRectOutline(t, cx - 12, cy - 12, cw + 24, chh + 24, 18, 5, Rgb(36, 84, 196));
		CoverOrCard(t, v, v.sel, cx, cy, cw, chh, Rgb(250, 230, 160), Rgb(236, 190, 90), Rgb(60, 40, 20));
		char count[48];
		snprintf(count, sizeof count, "%d / %d", v.sel + 1, n);
		CenterPx(cx + cw / 2, cy + chh + 30, count, 34, Rgb(255, 255, 255), true);
		const GameInfo& g = (*v.games)[size_t(v.sel)];
		std::string info = Upper(Info(g.system).name) + (g.region.empty() ? "" : "  " + Upper(g.region));
		CenterPx(200, 330, FitText(info, 2, 340), 28, Rgb(255, 255, 255), true);
		CenterPx(200, 380, Upper(v.family), 28, Rgb(255, 230, 120), true);
		if (!v.patch.empty())
		{
			CenterPx(200, 450, "ROM HACK", 26, Rgb(255, 120, 90), true);
			CenterPx(200, 490, FitText(Upper(v.patch), 2, 340), 24, Rgb(255, 255, 255), true);
		}
	}
	else
	{
		int y = top + 20;
		for (const std::string& line : v.empty_lines)
		{
			CenterPx(bx + bw / 2, y, FitText(Upper(line), 3, bw - 80), 30, Rgb(255, 230, 140), true);
			y += 56;
		}
	}
	if (!v.status.empty())
		DrawTextPx(60, 1036, FitText(Upper(v.status), 2, 800).c_str(), 24, Rgb(255, 255, 255), true);
	const std::string l1 = std::string(T("Press")) + " " + icon::Triangle + " " + T("for Options");
	const std::string l2 = std::string(T("Press")) + " " + icon::Square + " " + T("for ROM hacks");
	const std::string l3 = std::string(T("Press")) + " " + icon::Cross + " " + T("to Play");
	int y = 922;
	for (const std::string* s : {&l3, &l1, &l2})
	{
		const std::string u = Upper(*s);
		DrawTextPx(W - 60 - TextWidthPx(u.c_str(), 30), y, u.c_str(), 30, Rgb(255, 255, 255), true);
		y += 46;
	}
	const std::string nav = Upper(std::string(icon::DpadUpDown) + " " + T("Browse") + "   " + icon::L1 + " " + icon::R1 + " " +
		T("Storage") + "   " + icon::Options + " " + T("Quit"));
	DrawTextPx(60, 980, nav.c_str(), 26, Rgb(255, 255, 255), true);
}

// ---------------------------------------------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------------------------------------------
void FlowIcon(const draw::Target& t, int kind, int cx, int cy, bool active)
{
	const uint32_t c = active ? Rgb(255, 255, 255) : Rgb(110, 116, 128);
	if (active)
		draw::RoundRect(t, cx - 34, cy - 26, 68, 52, 14, Rgb(110, 200, 245), Rgb(40, 150, 220));
	switch (kind)
	{
		case 0: // star
			draw::Triangle(t, cx, cy - 18, cx - 16, cy + 14, cx + 16, cy + 14, c);
			draw::Triangle(t, cx - 18, cy - 6, cx + 18, cy - 6, cx, cy + 20, c);
			break;
		case 1: // search
			draw::Ring(t, cx - 4, cy - 4, 12, 4, c);
			draw::Triangle(t, cx + 4, cy + 8, cx + 8, cy + 4, cx + 18, cy + 16, c);
			break;
		case 2: // stars
			for (int k = -1; k <= 1; k++)
				draw::Circle(t, cx + k * 14, cy, 6, k == 0 ? Rgb(250, 200, 40) : c);
			break;
		case 3: // folder
			draw::RoundRect(t, cx - 18, cy - 10, 36, 24, 4, c, c);
			draw::RoundRect(t, cx - 18, cy - 16, 16, 8, 3, c, c);
			break;
		case 4: // list
			for (int k = -1; k <= 1; k++)
				draw::Fill(t, cx - 16, cy + k * 9 - 2, 32, 4, c);
			break;
		case 5: // grid
			for (int a = 0; a < 2; a++)
				for (int b = 0; b < 2; b++)
					draw::Fill(t, cx - 15 + a * 17, cy - 15 + b * 17, 13, 13, c);
			break;
		case 6: // cards
			draw::RoundRect(t, cx - 16, cy - 12, 22, 28, 3, c, c);
			draw::RoundRect(t, cx - 4, cy - 16, 22, 28, 3, Rgb(200, 204, 212), Rgb(200, 204, 212));
			break;
		default: // pad
			draw::RoundRect(t, cx - 20, cy - 10, 40, 20, 8, c, c);
			break;
	}
}

void DrawFlowChromeImpl(const LauncherView& v)
{
	const draw::Target t = draw::Screen();
	// top icon bar
	const int bw = 820, bx = (W - bw) / 2, by = 26, bh = 72;
	draw::RoundRect(t, bx, by, bw, bh, 36, Rgb(246, 246, 248), Rgb(222, 224, 230), 235);
	draw::RoundRectOutline(t, bx, by, bw, bh, 36, 3, Rgb(180, 184, 194));
	for (int k = 0; k < 8; k++)
		FlowIcon(t, k, bx + 70 + k * 97, by + bh / 2, k == 3);
	// arrows on both sides
	for (int side = 0; side < 2; side++)
	{
		const int ax = side == 0 ? 70 : W - 70, ay = 150;
		const int d = side == 0 ? -1 : 1;
		draw::Triangle(t, ax - d * 26, ay - 40, ax - d * 26, ay + 40, ax + d * 30, ay, Rgb(255, 255, 255));
		draw::Triangle(t, ax - d * 18, ay - 28, ax - d * 18, ay + 28, ax + d * 20, ay, Rgb(110, 196, 240));
	}
	// bottom panel: a wide curve
	draw::SoftEllipse(t, W / 2, 1240, 1500, 420, Rgb(250, 250, 252), 255);
	draw::Fill(t, 0, 1000, W, 80, Rgb(232, 234, 238));
	draw::VGradient(t, 0, 990, W, 90, Rgb(244, 245, 248), Rgb(214, 218, 226));
	for (int x = 0; x < W; x += 2)
	{
		// the panel's top edge: a soft arc line
		const double nx = (x - W / 2.0) / (W / 2.0);
		const int y = 880 + int(nx * nx * 110);
		draw::Fill(t, x, y, 2, 3, Rgb(160, 200, 230), 180);
		draw::Fill(t, x, y + 3, 2, 1080 - y - 3, Rgb(246, 246, 248), 255);
	}
	const int n = v.games ? int(v.games->size()) : 0;
	const uint32_t dark = Rgb(80, 84, 94);
	if (n > 0)
	{
		const GameInfo& g = (*v.games)[size_t(v.sel)];
		CenterPx(W / 2, 900, FitText(g.title, 4, 1100), 42, dark);
	}
	else if (!v.empty_lines.empty())
		CenterPx(W / 2, 900, FitText(v.empty_lines[0], 4, 1100), 42, dark);
	// the clock
	time_t now = time(nullptr);
	struct tm lt = {};
	localtime_r(&now, &lt);
	char clock[16];
	snprintf(clock, sizeof clock, "%02d:%02d", lt.tm_hour, lt.tm_min);
	CenterPx(W / 2, 950, clock, 70, Rgb(120, 124, 134));
	char count[96];
	snprintf(count, sizeof count, "%s  \xC2\xB7  %s %d", v.storage.c_str(), T("Games:"), v.total);
	CenterPx(W / 2, 1036, count, 26, dark);
	// round buttons: settings (left) and NES (right)
	for (int side = 0; side < 2; side++)
	{
		const int cx = side == 0 ? 210 : W - 210, cy = 980, r = 78;
		draw::Circle(t, cx + 4, cy + 6, r, Rgb(0, 0, 0), 40);
		draw::Circle(t, cx, cy, r, Rgb(248, 248, 250));
		draw::Ring(t, cx, cy, r, 6, Rgb(110, 196, 240));
		if (side == 0)
		{
			for (int k = 0; k < 8; k++)
			{
				const double a = k * M_PI / 4;
				draw::Circle(t, cx + int(std::cos(a) * 30), cy + int(std::sin(a) * 30), 10, Rgb(150, 156, 168));
			}
			draw::Circle(t, cx, cy, 28, Rgb(150, 156, 168));
			draw::Circle(t, cx, cy, 12, Rgb(248, 248, 250));
			DrawTextPx(cx + r + 14, cy - 16, icon::Triangle, 32, dark);
		}
		else
		{
			CenterPx(cx, cy - 26, "NES", 46, Rgb(150, 156, 168));
			DrawTextPx(cx - r - 14 - TextWidthPx(icon::Options, 32), cy - 16, icon::Options, 32, dark);
		}
	}
	// small icons beside the clock, as a channel bar has
	draw::RoundRect(t, W / 2 - 330, 990, 50, 64, 8, Rgb(250, 250, 252), Rgb(230, 232, 236));
	draw::RoundRectOutline(t, W / 2 - 330, 990, 50, 64, 8, 3, Rgb(170, 176, 188));
	draw::Fill(t, W / 2 - 320, 998, 30, 14, Rgb(170, 176, 188));
	draw::RoundRect(t, W / 2 + 280, 994, 80, 56, 10, Rgb(220, 236, 250), Rgb(170, 210, 240));
	draw::RoundRectOutline(t, W / 2 + 280, 994, 80, 56, 10, 3, Rgb(110, 170, 220));
	if (!v.status.empty())
		DrawTextPx(40, 1044, FitText(v.status, 2, 520).c_str(), 22, dark);
	if (!v.patch.empty())
	{
		const std::string s = std::string(T("ROM hack: ")) + v.patch;
		DrawTextPx(W - 40 - TextWidthPx(s.c_str(), 24), 1044, s.c_str(), 24, Rgb(40, 140, 210));
	}
	const std::string hints = LauncherHints(Theme::Flow, n > 0, false);
	CenterPx(W / 2, 112, FitText(hints, 2, W - 200), 24, dark);
}

// ---------------------------------------------------------------------------------------------------------------
// picker
// ---------------------------------------------------------------------------------------------------------------
// The previews: each theme drawn full size with a sample library, shrunk into its card.
void RenderPreview(Theme th, std::vector<uint32_t>& out, int pw, int ph)
{
	static const std::vector<GameInfo> sample = [] {
		std::vector<GameInfo> s;
		// invented names: the previews show the layout, not a real library
		const char* names[] = {"Brick Bandit", "Cosmo Courier", "Frost Hollow", "Lantern Keep", "Pixel Pilgrim",
			"Rocket Rodent", "Sky Lanterns", "Tiny Tank Rally", "Turbo Turnip", "Velvet Dungeon", "Wizard Wharf"};
		for (const char* n : names)
		{
			GameInfo g;
			g.title = n;
			g.region = "World";
			s.push_back(g);
		}
		return s;
	}();
	LauncherView v;
	v.games = &sample;
	v.sel = 3;
	v.pos = 3;
	v.storage = "Internal";
	v.family = "All games";
	v.total = int(sample.size());
	v.now = 0;
	const Face before = CurrentFace();
	SetFace(th == Theme::Station ? Face::Bungee : Face::VarelaRound);
	if (th == Theme::Flow)
	{
		DrawFlowBackground(ps5video::Surface(), W, H);
		// a flat stand-in for the 3D shelf
		const draw::Target t = draw::Screen();
		for (int k = -3; k <= 3; k++)
		{
			const int i = 3 + k;
			const int cw = k == 0 ? 360 : 230, ch = k == 0 ? 500 : 330;
			const int cx = W / 2 + k * 250 - cw / 2 + (k > 0 ? 70 : k < 0 ? -70 : 0);
			const int cy = 440 - ch / 2;
			if (k != 0)
				CoverOrCard(t, v, i, cx, cy, cw, ch, Rgb(236, 236, 240), Rgb(200, 200, 208), Rgb(60, 60, 70));
		}
		CoverOrCard(t, v, 3, W / 2 - 180, 190, 360, 500, Rgb(236, 236, 240), Rgb(200, 200, 208), Rgb(60, 60, 70));
		DrawFlowChromeImpl(v);
	}
	else
		DrawLauncher(th, v);
	SetFace(before);
	// shrink the surface into the preview (box filter)
	const uint32_t* s = ps5video::Surface();
	out.assign(size_t(pw) * ph, 0);
	for (int y = 0; y < ph; y++)
		for (int x = 0; x < pw; x++)
		{
			const int sx0 = x * W / pw, sx1 = std::max(sx0 + 1, (x + 1) * W / pw);
			const int sy0 = y * H / ph, sy1 = std::max(sy0 + 1, (y + 1) * H / ph);
			unsigned r = 0, g = 0, b = 0, c = 0;
			for (int yy = sy0; yy < sy1; yy += 2)
				for (int xx = sx0; xx < sx1; xx += 2)
				{
					const uint32_t p = s[size_t(yy) * W + xx];
					r += p & 0xff;
					g += (p >> 8) & 0xff;
					b += (p >> 16) & 0xff;
					c++;
				}
			out[size_t(y) * pw + x] = Rgb(uint8_t(r / c), uint8_t(g / c), uint8_t(b / c));
		}
}
} // namespace

const char* ThemeName(Theme t)
{
	switch (t)
	{
		case Theme::Station: return "Station";
		case Theme::Flow: return "Flow";
		default: return "Channel";
	}
}

const char* ThemeBlurb(Theme t)
{
	switch (t)
	{
		case Theme::Station: return "Sky, clouds and hills; a big list of names";
		case Theme::Flow: return "Your box art in a 3D cover flow";
		default: return "A framed list with the box art beside it";
	}
}

Theme CurrentTheme()
{
	const int t = Config().theme;
	return t >= 0 && t < int(Theme::Count) ? Theme(t) : Theme::Channel;
}

void ApplyTheme()
{
	using namespace ui;
	switch (CurrentTheme())
	{
		case Theme::Station:
			SetFace(Face::Bungee);
			kBg = Rgb(58, 132, 222);
			kPanel = Rgb(28, 74, 164);
			kAccent = Rgb(255, 204, 72);
			kSel = Rgb(70, 130, 230);
			kText = Rgb(255, 255, 255);
			kDim = Rgb(186, 214, 250);
			kFolder = Rgb(255, 224, 120);
			kGood = Rgb(140, 240, 160);
			kBad = Rgb(255, 150, 140);
			break;
		case Theme::Flow:
			SetFace(Face::VarelaRound);
			kBg = Rgb(228, 230, 236);
			kPanel = Rgb(248, 248, 250);
			kAccent = Rgb(36, 156, 220);
			kSel = Rgb(196, 228, 250);
			kText = Rgb(52, 54, 62);
			kDim = Rgb(124, 128, 140);
			kFolder = Rgb(220, 150, 30);
			kGood = Rgb(40, 160, 80);
			kBad = Rgb(210, 60, 60);
			break;
		default:
			SetFace(Face::VarelaRound);
			kBg = Rgb(214, 214, 220);
			kPanel = Rgb(252, 252, 252);
			kAccent = Rgb(212, 52, 60);
			kSel = Rgb(252, 206, 212);
			kText = Rgb(40, 40, 48);
			kDim = Rgb(118, 118, 128);
			kFolder = Rgb(200, 120, 20);
			kGood = Rgb(40, 150, 70);
			kBad = Rgb(200, 50, 50);
			break;
	}
}

std::string LauncherHints(Theme t, bool have_games, bool network_tab)
{
	const std::string sp = "     ";
	std::string s;
	if (have_games)
	{
		s += std::string(icon::Cross) + " " + T("Play") + sp;
		s += std::string(t == Theme::Flow ? icon::DpadLeftRight : icon::DpadUpDown) + " " + T("Browse") + sp;
		s += std::string(t == Theme::Flow ? icon::DpadUpDown : icon::DpadLeftRight) + " " +
			T(t == Theme::Flow ? "Letter" : "Page") + sp;
	}
	s += std::string(icon::L1) + " " + icon::R1 + " " + T("Storage") + sp;
	if (have_games || network_tab)
		s += std::string(icon::Square) + " " + T(network_tab ? "Scan again" : "Game options") + sp;
	s += std::string(icon::Triangle) + " " + T("Settings") + sp + icon::Options + " " + T("Quit");
	return s;
}

void DrawLauncher(Theme t, const LauncherView& v)
{
	if (t == Theme::Station)
		DrawStation(v);
	else
		DrawChannel(v);
}

void DrawFlowBackground(uint32_t* px, int w, int h)
{
	const draw::Target t{px, w, h, w};
	draw::VGradient(t, 0, 0, w, h * 62 / 100, Rgb(238, 238, 241), Rgb(206, 207, 213));
	draw::VGradient(t, 0, h * 62 / 100, w, h - h * 62 / 100, Rgb(226, 227, 232), Rgb(246, 246, 248));
	// a soft light behind the middle cover
	draw::SoftEllipse(t, w / 2, h * 40 / 100, w * 30 / 100, h * 34 / 100, Rgb(255, 255, 255), 150);
}

void DrawFlowChrome(const LauncherView& v)
{
	DrawFlowChromeImpl(v);
}

bool ThemePicker(bool first_time)
{
	constexpr int kCards = int(Theme::Count);
	const int pw = 512, ph = 288; // previews, 16:9
	std::vector<uint32_t> previews[kCards];
	for (int i = 0; i < kCards; i++)
		RenderPreview(Theme(i), previews[i], pw, ph);
	int sel = Config().theme_chosen ? int(CurrentTheme()) : 0;
	ui::NavReader nav;
	const Face before = CurrentFace();
	for (;;)
	{
		const ui::Nav in = nav.Read();
		if (in.left)
			sel = (sel + kCards - 1) % kCards;
		if (in.right)
			sel = (sel + 1) % kCards;
		if (in.ok)
		{
			Config().theme = sel;
			Config().theme_chosen = true;
			Config().Save();
			ApplyTheme();
			OrbisLog("[theme] %s chosen", ThemeName(Theme(sel)));
			return true;
		}
		if (in.back && !first_time)
		{
			SetFace(before);
			return false;
		}
		const draw::Target t = draw::Screen();
		draw::VGradient(t, 0, 0, W, H, Rgb(24, 26, 40), Rgb(10, 10, 18));
		SetFace(Face::VarelaRound);
		CenterPx(W / 2, 90, T("Choose your interface"), 72, Rgb(255, 255, 255));
		CenterPx(W / 2, 190, T(first_time ? "You can change it any time in Settings > Interface."
										  : "The library and the menus take the new look at once."),
			32, Rgb(170, 176, 200));
		for (int i = 0; i < kCards; i++)
		{
			const int cx = 96 + i * 600, cy = 300;
			const bool on = i == sel;
			const int lift = on ? -16 : 0;
			if (on)
				draw::RoundRect(t, cx - 14, cy - 14 + lift, pw + 28, ph + 220 + 28, 30, Rgb(110, 200, 255), Rgb(60, 140, 240));
			draw::RoundRect(t, cx, cy + lift, pw, ph + 220, 22, Rgb(46, 50, 70), Rgb(30, 32, 46));
			// the preview
			for (int y = 0; y < ph; y++)
				memcpy(ps5video::Surface() + size_t(cy + lift + y) * W + cx, &previews[i][size_t(y) * pw], size_t(pw) * 4);
			CenterPx(cx + pw / 2, cy + lift + ph + 36, T(ThemeName(Theme(i))), 52, on ? Rgb(255, 255, 255) : Rgb(200, 204, 220));
			CenterPx(cx + pw / 2, cy + lift + ph + 116, FitText(T(ThemeBlurb(Theme(i))), 2, pw - 30), 26, Rgb(170, 176, 200));
		}
		const std::string hint = std::string(icon::DpadLeftRight) + " " + T("Choose") + "        " + icon::Cross + " " +
			T("Use this one") + (first_time ? "" : std::string("        ") + icon::Circle + " " + T("Back"));
		CenterPx(W / 2, 960, hint, 36, Rgb(220, 224, 240));
		ps5video::Present(0, 0, 0, 0, true);
	}
}
} // namespace fe
