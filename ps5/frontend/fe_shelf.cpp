// fceuxs5 frontend: the launcher (fe_shelf.h) -- the library, its tabs, box art, settings and quitting, drawn by the
// interface theme the player chose (fe_theme.h): the Channel and Station lists, or the Flow theme's 3D cover flow,
// which is this file's shelf renderer on a light floor.
//
// From the MIT-licensed 3D shelf of MisterTemaki's PS5 ports; fceuxs5 adds the themes, the game options (ROM hacks)
// and the menu music.
// SPDX-License-Identifier: MIT

#include "fe_shelf.h"

#include "fe_covers.h"
#include "fe_games.h"
#include "fe_i18n.h"
#include "fe_menu.h"
#include "fe_prefetch.h"
#include "fe_settings.h"
#include "fe_tabs.h"
#include "fe_text.h"
#include "fe_theme.h"
#include "fe_media.h"
#include "fe_patch.h"

#include "OrbisPaths.h"
#include "ProsperoInput.h"
#include "ProsperoNotify.h"
#include "ProsperoSce.h"
#include "ProsperoCrash.h"
#include "ProsperoThread.h"
#include "ProsperoVideo.h"

#include "netfs.h"

#include <immintrin.h>

#include <strings.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

// ---- camera and layout (world units: the middle cover is kCoverH tall at distance kCamDist) ----------
constexpr float kFocal = 1000.0f;
constexpr float kCamDist = 1000.0f;
constexpr float kCoverH = 540.0f;
constexpr float kCamY = kCoverH * 0.42f; // eye height above the floor
constexpr float kHorizon = 455.0f; // screen row of the eye
constexpr float kSideX = 600.0f; // first side cover
constexpr float kSideStep = 165.0f; // the next ones
constexpr float kSideZ = 330.0f; // how far back the side covers stand
constexpr float kSideAngle = 1.10f; // radians (63 degrees)
constexpr float kVisible = 7.5f; // covers each side
constexpr float kReflect = 0.42f; // reflection length, of the cover's height
constexpr int kKeepRadius = 14; // textures kept around the selection

double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

inline uint32_t Lerp32(uint32_t a, uint32_t b, uint32_t w) // w 0..256
{
	const uint32_t iw = 256 - w;
	const uint32_t rb = (((a & 0xff00ffu) * iw + (b & 0xff00ffu) * w) >> 8) & 0xff00ffu;
	const uint32_t g = (((a & 0x00ff00u) * iw + (b & 0x00ff00u) * w) >> 8) & 0x00ff00u;
	return 0xff000000u | rb | g;
}

inline uint32_t Scale32(uint32_t a, uint32_t s) // s 0..256
{
	const uint32_t rb = (((a & 0xff00ffu) * s) >> 8) & 0xff00ffu;
	const uint32_t g = (((a & 0x00ff00u) * s) >> 8) & 0x00ff00u;
	return 0xff000000u | rb | g;
}

inline uint32_t Add32(uint32_t a, uint32_t b) // per channel, saturating
{
	return uint32_t(_mm_cvtsi128_si32(_mm_adds_epu8(_mm_cvtsi32_si128(int(a)), _mm_cvtsi32_si128(int(b))))) | 0xff000000u;
}

// ---- a small pool: the screen is split in vertical bands, one per thread --------------------------------
class Pool
{
public:
	Pool()
	{
		unsigned n = std::thread::hardware_concurrency();
		n = std::clamp(n == 0 ? 4u : n, 2u, 6u);
		// 1.2 (Snes9x PS5 2.10's, as Stella PS5 1.0.2): only the threads that really started count (the caller's band plus
		// theirs), as in ProsperoCrt.cpp's pool: Run() waits for m_count - 1 answers, and a thread the system refused
		// never answers -- the shelf would wait for ever on its first frame
		for (unsigned i = 1; i < n; i++)
		{
			ps5::BigThread t([this, i] { Worker(int(i)); }, 2 * 1024 * 1024);
			if (!t.joinable())
				break;
			m_threads.push_back(std::move(t));
		}
		m_count = 1 + int(m_threads.size());
	}
	~Pool()
	{
		{
			std::lock_guard<std::mutex> lock(m_lock);
			m_quit = true;
			m_gen++;
		}
		m_cv.notify_all();
		for (auto& t : m_threads)
			t.join();
	}
	// job(x0, x1) on every band, the calling thread included; returns when all are done.
	void Run(const std::function<void(int, int)>& job)
	{
		{
			std::lock_guard<std::mutex> lock(m_lock);
			m_job = &job;
			m_pending = m_count - 1;
			m_gen++;
		}
		m_cv.notify_all();
		Band(0, job);
		std::unique_lock<std::mutex> lock(m_lock);
		m_done.wait(lock, [this] { return m_pending == 0; });
		m_job = nullptr;
	}

private:
	void Band(int i, const std::function<void(int, int)>& job)
	{
		const int step = ((W / m_count) + 31) & ~31;
		const int x0 = i * step, x1 = std::min(W, x0 + step);
		if (x0 < x1)
			job(x0, x1);
	}
	void Worker(int i)
	{
		uint64_t seen = 0;
		for (;;)
		{
			const std::function<void(int, int)>* job;
			{
				std::unique_lock<std::mutex> lock(m_lock);
				m_cv.wait(lock, [&] { return m_gen != seen; });
				seen = m_gen;
				if (m_quit)
					return;
				job = m_job;
			}
			if (job)
				Band(i, *job);
			{
				std::lock_guard<std::mutex> lock(m_lock);
				if (--m_pending == 0)
					m_done.notify_one();
			}
		}
	}
	std::vector<ps5::BigThread> m_threads;
	int m_count = 1;
	std::mutex m_lock;
	std::condition_variable m_cv, m_done;
	uint64_t m_gen = 0;
	int m_pending = 0;
	bool m_quit = false;
	const std::function<void(int, int)>* m_job = nullptr;
};

// ---- one cover in 3D ------------------------------------------------------------------------------------
struct Placed
{
	const CoverTex* tex = nullptr;
	float d = 0; // offset from the selection (fractional while gliding)
	float cx = 0, dist = kCamDist; // centre: x, distance from the eye
	float a = 0, b = 0; // x(t) = cx + a*t, dist(t) = dist + b*t, t in [-0.5, 0.5]
	float wh = kCoverH; // world height
	uint32_t bright = 256; // 0..256
	uint32_t alpha = 256; // fade at the ends of the shelf
	int sx0 = 0, sx1 = 0; // screen columns
};

bool Place(Placed& p, const CoverTex* tex, float d)
{
	p.tex = tex;
	p.d = d;
	const float ad = std::abs(d), sgn = d < 0 ? -1.0f : 1.0f;
	const float k = std::min(ad, 1.0f);
	const float e = k * k * (3 - 2 * k); // smoothstep
	const float x = ad <= 1 ? sgn * kSideX * e : sgn * (kSideX + (ad - 1) * kSideStep);
	const float z = kSideZ * e;
	const float ang = kSideAngle * e;
	const float aspect = float(tex->w[0]) / float(tex->h[0]);
	const float ww = kCoverH * aspect;
	p.cx = x;
	p.dist = kCamDist + z;
	p.a = ww * std::cos(ang);
	// the end towards the middle stands further back: t = +0.5 for a left cover, -0.5 for a right one
	p.b = (d < 0 ? 1.0f : -1.0f) * ww * std::sin(ang);
	p.wh = kCoverH;
	float br = 1.0f - 0.40f * e - 0.07f * std::max(0.0f, ad - 1);
	p.bright = uint32_t(std::clamp(br, 0.25f, 1.0f) * 256);
	p.alpha = uint32_t(std::clamp((kVisible - ad) / 1.0f, 0.0f, 1.0f) * 256);
	// screen extent
	float xs[2];
	for (int i = 0; i < 2; i++)
	{
		const float t = i == 0 ? -0.5f : 0.5f;
		const float dd = p.dist + p.b * t;
		if (dd < 50)
			return false;
		xs[i] = W * 0.5f + kFocal * (p.cx + p.a * t) / dd;
	}
	p.sx0 = int(std::floor(std::min(xs[0], xs[1])));
	p.sx1 = int(std::ceil(std::max(xs[0], xs[1])));
	return p.sx1 > 0 && p.sx0 < W && p.alpha > 0;
}

// Draws columns [x0, x1) of one placed cover: the cover itself and its reflection on the floor.
void DrawCover(const Placed& p, uint32_t* surf, int x0, int x1)
{
	const CoverTex& t = *p.tex;
	const int cx0 = std::max(x0, p.sx0), cx1 = std::min(x1, p.sx1);
	for (int x = cx0; x < cx1; x++)
	{
		const float X = (x + 0.5f - W * 0.5f) / kFocal;
		const float den = p.a - X * p.b;
		if (std::abs(den) < 1e-4f)
			continue;
		const float tt = (X * p.dist - p.cx) / den; // -0.5 .. 0.5
		if (tt < -0.5f || tt > 0.5f)
			continue;
		const float dd = p.dist + p.b * tt;
		const float top = kHorizon - kFocal * (p.wh - kCamY) / dd;
		const float bot = kHorizon + kFocal * kCamY / dd;
		const float span = bot - top;
		// mip level from the vertical scale
		int l = 0;
		const float scale = span / float(t.h[0]);
		if (scale < 0.38f)
			l = 2;
		else if (scale < 0.75f)
			l = 1;
		const int tw = t.w[l], th = t.h[l];
		const uint32_t* px = t.px[l].data();
		const float u = (tt + 0.5f) * tw - 0.5f;
		const int u0 = std::clamp(int(std::floor(u)), 0, tw - 1), u1 = std::min(u0 + 1, tw - 1);
		const uint32_t fu = uint32_t(std::clamp(u - std::floor(u), 0.0f, 1.0f) * 256);
		const uint32_t* c0 = px + size_t(u0) * th;
		const uint32_t* c1 = px + size_t(u1) * th;
		const float dv = th / span;
		// edge columns: soften the vertical edges a little
		uint32_t edge = 256;
		if (x == p.sx0 || x == p.sx1 - 1)
			edge = 140;
		const uint32_t bright = p.bright;
		const uint32_t alpha = (p.alpha * edge) >> 8;

		// fixed point: v in 16.16
		const int32_t vmax = (th - 1) << 16;
		auto sample = [&](int32_t v) -> uint32_t {
			v = std::clamp(v, 0, vmax - 1);
			const int y0 = v >> 16;
			const uint32_t fv = uint32_t(v >> 8) & 0xff;
			const uint32_t a0 = Lerp32(c0[y0], c0[y0 + 1], fv);
			const uint32_t a1 = Lerp32(c1[y0], c1[y0 + 1], fv);
			return Lerp32(a0, a1, fu);
		};
		const int32_t dv16 = int32_t(dv * 65536.0f);

		// the cover: full rows in the middle, partial coverage on the first and last row
		const int ytop = std::max(0, int(std::floor(top)));
		const int ybot = std::min(H - 1, int(std::ceil(bot)) - 1);
		int32_t v = int32_t(((ytop + 0.5f - top) * dv - 0.5f) * 65536.0f);
		uint32_t* d = surf + size_t(ytop) * W + x;
		for (int y = ytop; y <= ybot; y++, v += dv16, d += W)
		{
			uint32_t a = alpha;
			if (y == ytop || y == ybot)
			{
				float cov = 1.0f;
				if (y < top)
					cov -= top - y;
				if (y + 1 > bot)
					cov -= (y + 1) - bot;
				if (cov <= 0)
					continue;
				a = (alpha * uint32_t(cov * 256)) >> 8;
			}
			const uint32_t c = Scale32(sample(v), bright);
			*d = a >= 256 ? c : Lerp32(*d, c, a);
		}
		// the reflection, fading out, mirrored from the cover's bottom edge up
		const float rlen = span * kReflect;
		const int r0 = std::max(0, int(std::ceil(bot)));
		const int r1 = std::min(H - 1, int(bot + rlen));
		const float inv_rlen = 1.0f / rlen;
		int32_t rv = int32_t((float(th - 1) - (r0 + 0.5f - bot) * dv) * 65536.0f);
		uint32_t* rd = surf + size_t(r0) * W + x;
		for (int y = r0; y <= r1; y++, rv -= dv16, rd += W)
		{
			const float k = (y + 0.5f - bot) * inv_rlen;
			if (k >= 1)
				break;
			const uint32_t a = uint32_t(0.30f * (1 - k) * (1 - k) * alpha);
			if (a == 0)
				continue;
			*rd = Lerp32(*rd, Scale32(sample(std::max(0, rv)), bright), a);
		}
	}
}

// The glow behind the middle cover, in the cover's average colour.
void DrawGlow(const Placed& p, uint32_t* surf, int x0, int x1, float strength)
{
	if (strength <= 0)
		return;
	const float top = kHorizon - kFocal * (p.wh - kCamY) / p.dist;
	const float bot = kHorizon + kFocal * kCamY / p.dist;
	const float l = float(p.sx0), r = float(p.sx1);
	const float R = 70.0f;
	const uint32_t col = p.tex->average;
	const int gx0 = std::max(x0, int(l - R)), gx1 = std::min(x1, int(r + R));
	const int gy0 = std::max(0, int(top - R)), gy1 = std::min(H, int(bot + R * 0.6f));
	for (int y = gy0; y < gy1; y++)
	{
		const float dy = std::max({top - y, 0.0f, y - bot});
		const bool inside_rows = dy == 0;
		uint32_t* row = surf + size_t(y) * W;
		for (int x = gx0; x < gx1; x++)
		{
			if (inside_rows && x >= int(l) && x < int(r))
			{
				x = int(r) - 1; // the cover covers this part
				continue;
			}
			const float dx = std::max({l - x, 0.0f, x - r});
			const float dist2 = dx * dx + dy * dy;
			if (dist2 >= R * R)
				continue;
			const float k = 1 - std::sqrt(dist2) / R;
			const uint32_t a = uint32_t(k * k * strength * 0.85f * 256);
			row[x] = Add32(row[x], Scale32(col, a));
		}
	}
}

// ---- input: left/right with key repeat that speeds up -------------------------------------------------
struct Repeat
{
	double next = 0, since = 0;
	bool held = false;
	bool Fire(bool down, double now)
	{
		if (!down)
		{
			held = false;
			return false;
		}
		if (!held)
		{
			held = true;
			since = now;
			next = now + 0.32;
			return true;
		}
		if (now >= next)
		{
			next = now + (now - since > 1.4 ? 0.045 : 0.095);
			return true;
		}
		return false;
	}
};

void CenterText(int y, const std::string& s, int scale, uint32_t color)
{
	const std::string fit = FitText(T(s.c_str()), scale, W - 200);
	DrawText((W - TextWidth(fit.c_str(), scale)) / 2, y, fit.c_str(), scale, color);
}

bool Confirm(const char* question)
{
	std::vector<uint32_t> saved(ps5video::Surface(), ps5video::Surface() + size_t(W) * H);
	ps5input::Poll();
	uint32_t prev = ps5input::Pad(0).buttons;
	ps5video::DarkenRect(0, 0, W, H);
	const int bw = 900, bh = 240, bx = (W - bw) / 2, by = (H - bh) / 2;
	ps5video::FillRect(bx - 3, by - 3, bw + 6, bh + 6, Rgb(150, 125, 255));
	ps5video::FillRect(bx, by, bw, bh, Rgb(26, 24, 48));
	DrawText(bx + 50, by + 50, FitText(T(question), 5, bw - 100).c_str(), 5, Rgb(240, 240, 250));
	DrawText(bx + 50, by + 160, (std::string(icon::Cross) + " " + T("Yes") + "       " + icon::Circle + " " + T("No")).c_str(), 3, Rgb(170, 160, 210));
	ps5video::Present(0, 0, 0, 0, true);
	for (;;)
	{
		ps5input::Poll();
		const uint32_t cur = ps5input::Pad(0).buttons;
		const uint32_t down = cur & ~prev;
		prev = cur;
		if (down & SCE_PAD_BUTTON_CROSS)
			return true;
		if (down & (SCE_PAD_BUTTON_CIRCLE | SCE_PAD_BUTTON_OPTIONS))
		{
			memcpy(ps5video::Surface(), saved.data(), saved.size() * 4);
			return false;
		}
		ps5video::Present(0, 0, 1, 1, true);
	}
}

// ---- state kept between visits to the shelf (the game list, its covers) -----------------------------------
struct ShelfState
{
	std::vector<GameInfo> all; // the whole library
	int family = -1; // the tab the list below shows
	std::vector<GameInfo> games; // the games on the shelf: the tab's
	CoverService covers;
	std::vector<CoverPtr> slots;
	bool started = false;
	bool downloads = true;
	std::string tab_sel[tabs::kCount]; // each storage tab's selected game, kept between visits to the shelf
};
ShelfState& S()
{
	static ShelfState* s = new ShelfState(); // never destroyed: the cover thread may outlive main's statics
	return *s;
}

std::shared_ptr<CoverTex> LoadingCard()
{
	static std::shared_ptr<CoverTex> card;
	if (card)
		return card;
	const int w = 716, h = kCoverTexH;
	auto tex = std::make_shared<CoverTex>();
	int lw = w, lh = h;
	for (int l = 0; l < CoverTex::kLevels; l++)
	{
		tex->w[l] = lw;
		tex->h[l] = lh;
		tex->px[l].resize(size_t(lw) * lh);
		for (int x = 0; x < lw; x++)
			for (int y = 0; y < lh; y++)
			{
				const bool border = x < 3 || y < 3 || x >= lw - 3 || y >= lh - 3;
				const float t = float(y) / lh;
				tex->px[l][size_t(x) * lh + y] = border ? Rgb(80, 70, 130) : Rgb(uint8_t(36 - 14 * t), uint8_t(32 - 12 * t), uint8_t(64 - 26 * t));
			}
		lw /= 2;
		lh /= 2;
	}
	tex->average = Rgb(60, 50, 110);
	card = tex;
	return card;
}

// The tabs that have games: "All games" first, then "#" and the letters A to Z that some title starts with.
std::vector<Family> Tabs(const std::vector<GameInfo>& all)
{
	bool used[int(Family::Count)] = {};
	for (const GameInfo& g : all)
		used[int(FamilyOf(g.title))] = true;
	std::vector<Family> tabs = {Family::All};
	for (int f = 1; f < int(Family::Count); f++)
		if (used[f])
			tabs.push_back(Family(f));
	return tabs;
}

std::vector<GameInfo> InFamily(const std::vector<GameInfo>& all, Family f)
{
	if (f == Family::All)
		return all;
	std::vector<GameInfo> out;
	for (const GameInfo& g : all)
		if (FamilyOf(g.title) == f)
			out.push_back(g);
	return out;
}

void Loading(const char* text)
{
	ps5video::FillRect(0, 0, W, H, Rgb(12, 10, 26));
	DrawText(80, 50, "fceuxs5", 6, Rgb(150, 125, 255));
	CenterText(H / 2 - 20, text, 4, Rgb(220, 220, 235));
	ps5video::Present(0, 0, 0, 0, true);
}

// A line already in the player's language (a formatted one), centred.
[[maybe_unused]] void CenterRaw(int y, const std::string& s, int scale, uint32_t color)
{
	const std::string fit = FitText(s, scale, W - 200);
	DrawText((W - TextWidth(fit.c_str(), scale)) / 2, y, fit.c_str(), scale, color);
}

// ---- storage tabs (Internal, USB, Network: fe_tabs.h), and what the shares are doing (the Network tab) ---------
// From Snes9x PS5 (thatboialex's shelf tabs). The system tabs (Up / Down) work inside the storage tab.
// A tab's name on the screen (USB is a name; the log keeps tabs::Name in English).
const char* TabLabel(int t)
{
	return t == tabs::Usb ? tabs::Name(t) : T(tabs::Name(t));
}

// The tab bar, centred at the top: Internal, USB (only with a USB game), Network. L1 / R1 stand at its ends.
constexpr int kTabsY = 24, kTabsH = 54;
[[maybe_unused]] void DrawTabs(const int counts[tabs::kCount], int active)
{
	constexpr int scale = 3, padx = 30, gap = 16, y = kTabsY, h = kTabsH;
	int widths[tabs::kCount] = {};
	int total = 0, shown = 0;
	for (int t = 0; t < tabs::kCount; t++)
		if (tabs::Shown(counts, t))
			total += (widths[t] = TextWidth(TabLabel(t), scale) + 2 * padx) + (shown++ ? gap : 0);
	int x = (W - total) / 2;
	const uint32_t dim = Rgb(150, 146, 185);
	if (shown > 1)
	{
		DrawText(x - 24 - TextWidth(icon::L1, scale), y + 12, icon::L1, scale, dim);
		DrawText(x + total + 24, y + 12, icon::R1, scale, dim);
	}
	for (int t = 0; t < tabs::kCount; t++)
	{
		if (!widths[t])
			continue;
		const bool on = t == active;
		ps5video::FillRect(x, y, widths[t], h, on ? Rgb(78, 62, 170) : Rgb(32, 30, 58));
		if (on)
			ps5video::FillRect(x, y + h - 4, widths[t], 4, Rgb(170, 150, 255));
		DrawText(x + padx, y + 12, TabLabel(t), scale, on ? Rgb(245, 245, 252) : dim);
		x += widths[t] + gap;
	}
}

struct NetLine
{
	std::string text; // in the player's language
	uint32_t color;
	bool ready; // a share that is up: only an empty Network tab lists those
};
struct NetView
{
	std::vector<NetLine> lines;
	int shares = 0, waiting = 0, ready = 0;
};

// What the shares answer, a line for each share. `since` is when each was first seen connecting.
NetView NetStatus(std::map<std::string, double>& since, double now)
{
	NetView v;
	for (const NetfsShareInfo& s : netfs_shares())
	{
		v.shares++;
		if (s.state == NetfsState::Idle || s.state == NetfsState::Connecting)
		{
			v.waiting++;
			const double t0 = since.emplace(s.mount_point, now).first->second;
			char text[256];
			snprintf(text, sizeof(text), T("Connecting to %s... %d s"), s.label.c_str(), int(now - t0));
			v.lines.push_back({text, Rgb(205, 200, 235), false});
			continue;
		}
		since.erase(s.mount_point);
		if (s.state == NetfsState::Ready)
		{
			v.ready++;
			char text[256];
			snprintf(text, sizeof(text), T("%s: connected, no games found."), s.label.c_str());
			v.lines.push_back({text, Rgb(205, 200, 235), true});
		}
		else
		{
			// netfs's own reason stays in English (it is the log's text too)
			char text[256];
			snprintf(text, sizeof(text), T("%s is not available."), s.label.c_str());
			v.lines.push_back({s.error.empty() ? std::string(text) : s.error, Rgb(255, 150, 150), false});
		}
	}
	return v;
}
} // namespace

void ShelfShutdown()
{
	S().covers.Stop();
}

std::string Shelf()
{
	Settings& cfg = Config();
	ShelfState& st = S();
	FCEUXS5_STAGE(Shelf, "pool");
	Pool pool;

	FCEUXS5_STAGE(Shelf, "scan games");
	Loading("Looking for games...");
	std::vector<std::string> net_seen = netfs_mount_points(); // taken first: a share that connects during the scan is seen below
	std::vector<GameInfo> games = ScanGames();
	bool same = games.size() == st.all.size();
	for (size_t i = 0; same && i < games.size(); i++)
		same = games[i].path == st.all[i].path;
	if (!ShelfDownloads() && (!same || !st.started))
	{
		// the console: covers come from the prefetch at start (fe_prefetch.h); new ones restart the app
		FCEUXS5_STAGE(Shelf, "wanted covers");
		CoversRestartIfNeeded(games, cfg.covers_download, false, Loading);
	}
	st.all = games;
	NetNaming naming; // names the games on shares by CRC while the shelf is up; stops with it
	naming.Add(st.all);
	int naming_seen = 0;
	double naming_refreshed = Now();

	// -- tabs: the storage tab (L1 / R1) and, inside it, the system tab (Up / Down); st.games is what both leave
	for (const GameInfo& g : st.all)
		if (g.path == cfg.last_rom)
			st.tab_sel[tabs::Of(g)] = g.path; // the game played last is where its tab opens
	int counts[tabs::kCount];
	int stab = cfg.last_tab;
	std::vector<Family> ftabs = {Family::All};
	int tab = 0;
	int n = 0, sel = 0;
	float pos = 0;
	double sel_since = Now();
	double tab_since = -10.0;
	bool tab_chosen = false; // the player pressed L1 / R1: an empty tab is then left as it is
	std::map<std::string, double> net_since; // when each share was first seen connecting
	NetView net;
	std::string net_key;
	bool rescan = false; // Square on the Network tab
	// The games of the storage tab and the system tab, the selection on `keep` (else the first game). restart: the
	// cover service starts again even when the list is the same (the shelf opens on a library that changed).
	auto Fill = [&](const std::string& keep, bool restart) {
		std::vector<GameInfo> local;
		for (const GameInfo& g : st.all)
			if (tabs::Of(g) == stab)
				local.push_back(g);
		ftabs = Tabs(local);
		tab = 0;
		for (size_t i = 0; i < ftabs.size(); i++)
			if (int(ftabs[i]) == cfg.shelf_family)
				tab = int(i);
		std::vector<GameInfo> list = InFamily(local, ftabs[size_t(tab)]);
		// the same games, named the same (the cover is looked up by these): the covers are left as they are
		bool same_list = list.size() == st.games.size();
		for (size_t i = 0; same_list && i < list.size(); i++)
		{
			const GameInfo &a = list[i], &b = st.games[i];
			same_list = a.path == b.path && a.system == b.system && a.nointro == b.nointro && a.title == b.title;
		}
		if (restart || !same_list || !st.started || st.downloads != cfg.covers_download || st.family != int(ftabs[size_t(tab)]))
		{
			st.family = int(ftabs[size_t(tab)]);
			st.games = std::move(list);
			st.slots.assign(st.games.size(), nullptr);
			st.downloads = cfg.covers_download;
			FCEUXS5_STAGE(Shelf, "cover service start");
			st.covers.Start(st.games, cfg.covers_download && ShelfDownloads(), cfg.covers_download && BackgroundCovers());
			st.started = true;
		}
		n = int(st.games.size());
		sel = 0;
		for (int i = 0; i < n; i++)
			if (st.games[size_t(i)].path == keep)
				sel = i;
		pos = float(sel);
		sel_since = Now();
		if (n > 0)
			st.covers.SetFocus(sel);
	};
	// Opens storage tab `want` (or the tab that fits when it has nothing to show, see tabs::Resolve) on the game
	// `keep`, or on the one that tab had selected.
	auto OpenTab = [&](int want, const std::string& keep, bool restart) {
		tabs::Count(st.all, counts);
		const bool share_set_up = !netfs_shares().empty();
		stab = tabs::Resolve(counts, want, tab_chosen || (want == tabs::Network && share_set_up), share_set_up);
		Fill(keep.empty() ? st.tab_sel[stab] : keep, restart);
		cfg.last_tab = stab;
		OrbisLog("[shelf] tab %s: %d game(s)", tabs::Name(stab), n);
	};
	// what the shares answer, for the Network tab; true when a line changed (a second ticked, a share came or went)
	auto RefreshNet = [&](double at) {
		net = NetStatus(net_since, at);
		std::string key;
		for (const NetLine& l : net.lines)
			key += l.text + "\n";
		const bool changed = key != net_key;
		net_key = key;
		return changed;
	};
	OpenTab(cfg.last_tab, "", !same);
	RefreshNet(Now());

	float fade = 1.0f;
	FCEUXS5_STAGE(Shelf, "first frame");
	const CoverTex* bd_wanted = nullptr;
	ApplyTheme();
	MusicStart();

	Repeat rep_l, rep_r, rep_l2, rep_r2;
	ps5input::Poll();
	uint32_t prev = ps5input::Pad(0).buttons;
	double last = Now();
	bool dirty = true;
	double next_net = Now() + 0.5;
	int last_status = -1;

	for (;;)
	{
		FCEUXS5_STAGE(Shelf, "loop");
		const double now = Now();
		const float dt = float(std::min(0.05, now - last));
		last = now;

		// -- shares: one that connected (or dropped out) after the scan changes what is on the shelf
		if (now >= next_net)
		{
			next_net = now + 0.5;
			for (const std::string& ev : netfs_take_events())
			{
				OrbisLog("[net] %s", ev.c_str());
				ProsperoNotify("fceuxs5: %s", ev.c_str());
			}
			std::vector<std::string> mounts = netfs_mount_points();
			// names from the CRC pass count when it is done, or a minute after the last look
			const int resolved = naming.Resolved();
			const bool named = resolved != naming_seen && (!naming.Busy() || now - naming_refreshed >= 60.0);
			if (mounts != net_seen || named || rescan)
			{
				if (rescan)
					Loading("Scanning the network shares...");
				rescan = false;
				net_seen = mounts;
				naming_seen = resolved;
				naming_refreshed = now;
				const std::string keep = n > 0 ? st.games[size_t(sel)].path : std::string();
				std::vector<GameInfo> fresh = ScanGames();
				if (!ShelfDownloads()) // the console: covers of games that came late are fetched by the prefetch of a restart
					CoversRestartIfNeeded(fresh, cfg.covers_download, false, Loading);
				st.all = std::move(fresh);
				naming.Add(st.all);
				OpenTab(stab, keep, false); // the covers start again only when the tab's games or their names changed
				dirty = true;
			}
			if (RefreshNet(now) && stab == tabs::Network)
				dirty = true;
		}

		// -- input
		ps5input::Poll();
		const uint32_t cur = ps5input::Pad(0).buttons;
		const uint32_t down = cur & ~prev;
		prev = cur;
		const int old_sel = sel;
		// the Flow theme browses across (Left / Right; Up / Down the letter); the lists browse down (Up / Down;
		// Left / Right a page; L2 / R2 the letter)
		const bool flow = CurrentTheme() == Theme::Flow;
		const uint32_t b_prev = flow ? SCE_PAD_BUTTON_LEFT : SCE_PAD_BUTTON_UP;
		const uint32_t b_next = flow ? SCE_PAD_BUTTON_RIGHT : SCE_PAD_BUTTON_DOWN;
		const uint32_t b_pg_prev = flow ? SCE_PAD_BUTTON_L2 : SCE_PAD_BUTTON_LEFT;
		const uint32_t b_pg_next = flow ? SCE_PAD_BUTTON_R2 : SCE_PAD_BUTTON_RIGHT;
		const uint32_t b_tab_next = flow ? SCE_PAD_BUTTON_DOWN : SCE_PAD_BUTTON_R2;
		const uint32_t b_tabs = flow ? (SCE_PAD_BUTTON_UP | SCE_PAD_BUTTON_DOWN) : (SCE_PAD_BUTTON_L2 | SCE_PAD_BUTTON_R2);
		const int page = flow ? 10 : 9;
		if (n > 0)
		{
			if (rep_l.Fire(cur & b_prev, now))
				sel = std::max(0, sel - 1);
			if (rep_r.Fire(cur & b_next, now))
				sel = std::min(n - 1, sel + 1);
			if (rep_l2.Fire(cur & b_pg_prev, now))
				sel = std::max(0, sel - page);
			if (rep_r2.Fire(cur & b_pg_next, now))
				sel = std::min(n - 1, sel + page);
		}
		if ((down & b_tabs) && ftabs.size() > 1)
		{
			// another tab (All, #, A..Z): the shelf shows its games, the selection stays on the same game if it is there
			const std::string keep = n > 0 ? st.games[size_t(sel)].path : cfg.last_rom;
			const int dir = (down & b_tab_next) ? 1 : -1;
			cfg.shelf_family = int(ftabs[size_t((tab + dir + int(ftabs.size())) % int(ftabs.size()))]);
			Fill(keep, false);
			tab_since = now;
			OrbisLog("[shelf] tab %s: %d game(s)", FamilyName(ftabs[size_t(tab)]), n);
			dirty = true;
		}
		if (down & (SCE_PAD_BUTTON_L1 | SCE_PAD_BUTTON_R1))
		{
			// another storage tab (Internal, USB, Network): it opens on the game it had selected
			if (n > 0)
				st.tab_sel[stab] = st.games[size_t(sel)].path;
			tab_chosen = true;
			OpenTab(tabs::Step(counts, stab, (down & SCE_PAD_BUTTON_R1) ? 1 : -1), "", false);
			dirty = true;
		}
		if (sel != old_sel)
		{
			st.covers.SetFocus(sel);
			sel_since = now;
			dirty = true;
		}
		if ((down & SCE_PAD_BUTTON_CROSS) && n > 0)
		{
			cfg.last_rom = st.games[size_t(sel)].path;
			cfg.last_tab = stab;
			st.tab_sel[stab] = cfg.last_rom;
			cfg.Save();
			MusicStop();
			return st.games[size_t(sel)].path;
		}
		if (down & SCE_PAD_BUTTON_TRIANGLE)
		{
			// Settings > Network shares connects the shares again (netfs_reload), which needs no other netfs call
			// running: the naming pass waits, and goes on (with what it had left) afterwards
			naming.Stop();
			SettingsMenu();
			ApplyTheme(); // Settings > Interface may have changed it
			MusicApplySettings();
			if (!QuitForUpdate().empty())
			{
				cfg.last_tab = stab;
				cfg.Save();
				return ""; // an update installed: the app ends, to be opened again
			}
			naming.Add(st.all);
			if (st.downloads != cfg.covers_download)
			{
				st.downloads = cfg.covers_download;
				st.covers.Start(st.games, cfg.covers_download && ShelfDownloads(), cfg.covers_download && BackgroundCovers());
				st.slots.assign(st.games.size(), nullptr);
			}
			ps5input::Poll();
			prev = ps5input::Pad(0).buttons;
			dirty = true;
		}
		if ((down & SCE_PAD_BUTTON_SQUARE) && stab == tabs::Network)
		{
			// scan again, as PSFlycast's Square does on its Network tab: forget the folder listings, and connect the
			// shares that are down once more (that reloads them all, so the games come back as they connect)
			bool share_down = false;
			for (const NetfsShareInfo& s : netfs_shares())
				share_down = share_down || s.state == NetfsState::Failed || s.state == NetfsState::Dead;
			netfs_rescan();
			if (share_down)
			{
				naming.Stop(); // it reads files of the shares that are about to be disconnected
				netfs_reload();
				net_since.clear();
			}
			rescan = !share_down;
			next_net = now;
			dirty = true;
			OrbisLog("[shelf] network tab: scan again%s", share_down ? ", reconnecting" : "");
		}
		else if ((down & SCE_PAD_BUTTON_SQUARE) && n > 0)
		{
			// the game's options: its ROM hack, its box art again, play
			bool refetch = false;
			const GameAction act = GameOptions(st.games[size_t(sel)], &refetch);
			ps5input::Poll();
			prev = ps5input::Pad(0).buttons;
			dirty = true;
			if (act == GameAction::Play)
			{
				cfg.last_rom = st.games[size_t(sel)].path;
				cfg.last_tab = stab;
				st.tab_sel[stab] = cfg.last_rom;
				cfg.Save();
				MusicStop();
				return st.games[size_t(sel)].path;
			}
			if (!refetch)
				continue;
			st.covers.Refetch(sel);
			st.slots[size_t(sel)] = nullptr;
			dirty = true;
			if (!ShelfDownloads()) // the console: the helper fetches it (or the prefetch at the next start)
			{
				cfg.last_rom = st.games[size_t(sel)].path;
				cfg.Save();
				CoversRestartIfNeeded(st.all, cfg.covers_download, true, Loading);
			}
		}
		if (down & SCE_PAD_BUTTON_OPTIONS)
		{
			if (Confirm("Quit fceuxs5?"))
			{
				cfg.last_tab = stab;
				cfg.Save();
				MusicStop();
				return "";
			}
			ps5input::Poll();
			prev = ps5input::Pad(0).buttons;
			dirty = true;
		}

		// -- covers that arrived
		const size_t before = std::count_if(st.slots.begin(), st.slots.end(), [](const CoverPtr& c) { return c != nullptr; });
		st.covers.Collect(st.slots, kKeepRadius);
		const size_t after = std::count_if(st.slots.begin(), st.slots.end(), [](const CoverPtr& c) { return c != nullptr; });
		if (after != before)
			dirty = true;

		// -- glide
		const float target = float(sel);
		if (std::abs(target - pos) > 0.0005f)
		{
			pos += (target - pos) * (1.0f - std::exp(-dt * 11.0f));
			if (std::abs(target - pos) < 0.0005f)
				pos = target;
			dirty = true;
		}

		// fceuxs5: the Flow theme's covers stand on a light floor (fe_theme.cpp), not on a blurred copy of the cover
		const CoverTex* sel_tex = (n > 0 && st.slots[size_t(sel)]) ? st.slots[size_t(sel)].get() : nullptr;
		(void)bd_wanted;
		(void)fade;
		(void)sel_tex;
		(void)tab_since;

		const int status = (st.covers.ToDownload() + st.covers.BackgroundLeft()) * 1000 + st.covers.Downloaded() +
						   (st.covers.BackgroundOffline() ? 1 << 30 : 0);
		if (status != last_status)
		{
			last_status = status;
			dirty = true;
		}

		// -- what the theme shows
		LauncherView view;
		view.games = &st.games;
		view.covers = &st.slots;
		view.sel = sel;
		view.pos = pos;
		view.storage = T(TabLabel(stab));
		view.family = T(FamilyName(ftabs[size_t(tab)]));
		view.total = int(st.all.size());
		view.now = now;
		if (cfg.covers_download)
		{
			char s[96] = "";
			if (st.covers.ToDownload() > 0 && !st.covers.Offline())
				snprintf(s, sizeof(s), T("Downloading covers... %d left"), st.covers.ToDownload());
			else if (st.covers.BackgroundLeft() > 0 && !st.covers.BackgroundOffline())
				snprintf(s, sizeof(s), T("Downloading covers in the background... %d left"), st.covers.BackgroundLeft());
			else if (st.covers.Offline() || (st.covers.BackgroundLeft() > 0 && st.covers.BackgroundOffline()))
				snprintf(s, sizeof(s), "%s", T("Offline: covers next time"));
			view.status = s;
		}
		if (n > 0)
		{
			// the ROM hack the game starts with (looked up when the selection changes)
			static std::string patch_for, patch_label;
			if (patch_for != st.games[size_t(sel)].path)
			{
				patch_for = st.games[size_t(sel)].path;
				const std::string p = PatchForGame(patch_for);
				patch_label = p.empty() ? "" : PatchLabel(p);
			}
			view.patch = patch_label;
		}
		else if (stab == tabs::Network)
		{
			const bool plural = net.shares > 1;
			view.empty_lines.push_back(T(net.shares == 0 ? "No network shares yet."
				: net.waiting ? "Looking for network games..."
				: net.ready == 0 ? (plural ? "Could not reach the network shares." : "Could not reach the network share.")
				: (plural ? "No games found on the network shares." : "No games found on the network share.")));
			for (size_t i = 0; i < net.lines.size() && i < 4; i++)
				view.empty_lines.push_back(net.lines[i].text);
			if (net.shares == 0)
				view.empty_lines.push_back(std::string(T("Add one:")) + "  " + icon::Triangle + "  " + T("Settings > Network shares"));
		}
		else
		{
			view.empty_lines = {T("No games found."), T("Copy your NES games (.nes .unf .fds .zip .7z) to"),
				"/data/fceuxs5/roms", T("or  fceuxs5/roms  on a USB drive, then open fceuxs5 again."),
				T("ROM hacks: put the .ips / .bps / .ups next to the game."),
				T("Famicom Disk System games need disksys.rom in /data/fceuxs5/bios.")};
		}
		if (!flow)
		{
			// Channel / Station: the theme draws the whole frame (Station's selection glows: always redrawn)
			if (!dirty && CurrentTheme() != Theme::Station)
			{
				ps5video::Present(0, 0, 1, 1, true);
				continue;
			}
			dirty = false;
			DrawLauncher(CurrentTheme(), view);
			ps5video::Present(0, 0, 0, 0, true);
			continue;
		}

		if (!dirty)
		{
			// nothing moved: flip a 1-pixel change so the loop still runs at the display's pace
			ps5video::Present(0, 0, 1, 1, true);
			continue;
		}
		dirty = false;

		// -- place the covers, far ones first
		std::vector<Placed> placed;
		if (n > 0)
		{
			const int lo = std::max(0, int(std::floor(pos - kVisible)));
			const int hi = std::min(n - 1, int(std::ceil(pos + kVisible)));
			for (int i = lo; i <= hi; i++)
			{
				const CoverTex* tex = st.slots[size_t(i)] ? st.slots[size_t(i)].get() : LoadingCard().get();
				Placed p;
				if (Place(p, tex, float(i) - pos))
					placed.push_back(p);
			}
			std::sort(placed.begin(), placed.end(), [](const Placed& a, const Placed& b) { return std::abs(a.d) > std::abs(b.d); });
		}
		uint32_t* surf = ps5video::Surface();
		static std::vector<uint32_t> flow_bg;
		if (flow_bg.empty())
		{
			flow_bg.resize(size_t(W) * H);
			DrawFlowBackground(flow_bg.data(), W, H);
		}
		const double t_render = Now();
		pool.Run([&](int x0, int x1) {
			// the floor
			for (int y = 0; y < H; y++)
				memcpy(surf + size_t(y) * W + x0, flow_bg.data() + size_t(y) * W + x0, size_t(x1 - x0) * 4);
			// covers; the glow goes right before the middle one
			for (const Placed& p : placed)
			{
				if (std::abs(p.d) < 0.5f)
					DrawGlow(p, surf, x0, x1, 1.0f - std::abs(p.d) * 2.0f);
				DrawCover(p, surf, x0, x1);
			}
		});

		{
			static double sum = 0;
			static int frames = 0;
			sum += Now() - t_render;
			if (++frames == 60)
			{
				OrbisLog("[shelf] 3D pass: %.2f ms a frame (average of 60)", sum / frames * 1000.0);
				sum = 0;
				frames = 0;
			}
		}
		// -- the Flow theme's panels over the covers
		DrawFlowChrome(view);

		ps5video::Present(0, 0, 0, 0, true);
	}
}
} // namespace fe
