// fceuxs5 frontend: 2D drawing for the interface themes (fe_draw.h).
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_draw.h"

#include "ProsperoVideo.h"
#include "fe_covers.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fe
{
namespace draw
{
namespace
{
inline int R(uint32_t c)
{
	return int(c & 0xff);
}
inline int G(uint32_t c)
{
	return int((c >> 8) & 0xff);
}
inline int B(uint32_t c)
{
	return int((c >> 16) & 0xff);
}
inline uint32_t Pack(int r, int g, int b)
{
	return 0xff000000u | (uint32_t(std::clamp(b, 0, 255)) << 16) | (uint32_t(std::clamp(g, 0, 255)) << 8) |
		uint32_t(std::clamp(r, 0, 255));
}

inline uint32_t BlendPx(uint32_t dst, uint32_t color, int a) // a 0..255
{
	if (a <= 0)
		return dst;
	if (a >= 255)
		return color | 0xff000000u;
	const uint32_t w = uint32_t(a) + (uint32_t(a) >> 7);
	const uint32_t iw = 256 - w;
	const uint32_t rb = (((dst & 0xff00ffu) * iw + (color & 0xff00ffu) * w) >> 8) & 0xff00ffu;
	const uint32_t g = (((dst & 0x00ff00u) * iw + (color & 0x00ff00u) * w) >> 8) & 0x00ff00u;
	return 0xff000000u | rb | g;
}

inline void Put(const Target& t, int x, int y, uint32_t c, int a)
{
	if (x < 0 || y < 0 || x >= t.w || y >= t.h)
		return;
	uint32_t& p = t.px[size_t(y) * t.pitch + x];
	p = BlendPx(p, c, a);
}

// coverage 0..1 of a pixel at (px, py) inside the rounded rectangle (x0, y0)-(x1, y1) with radius r
float RoundCoverage(float px, float py, float x0, float y0, float x1, float y1, float r)
{
	const float cx = std::clamp(px, x0 + r, x1 - r);
	const float cy = std::clamp(py, y0 + r, y1 - r);
	const float dx = px - cx, dy = py - cy;
	const float d = std::sqrt(dx * dx + dy * dy) - r; // < 0 inside
	return std::clamp(0.5f - d, 0.0f, 1.0f);
}
} // namespace

Target Screen()
{
	return Target{ps5video::Surface(), ps5video::kWidth, ps5video::kHeight, ps5video::kWidth};
}

uint32_t Mix(uint32_t a, uint32_t b, int t)
{
	t = std::clamp(t, 0, 256);
	return Pack(R(a) + (R(b) - R(a)) * t / 256, G(a) + (G(b) - G(a)) * t / 256, B(a) + (B(b) - B(a)) * t / 256);
}

uint32_t Shade(uint32_t c, int percent)
{
	if (percent <= 100)
		return Pack(R(c) * percent / 100, G(c) * percent / 100, B(c) * percent / 100);
	const int t = (percent - 100) * 256 / 100;
	return Mix(c, Pack(255, 255, 255), t);
}

void Fill(const Target& t, int x, int y, int w, int h, uint32_t color, int alpha)
{
	const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(t.w, x + w), y1 = std::min(t.h, y + h);
	for (int yy = y0; yy < y1; yy++)
	{
		uint32_t* row = t.px + size_t(yy) * t.pitch;
		if (alpha >= 255)
			std::fill(row + x0, row + x1, color | 0xff000000u);
		else
			for (int xx = x0; xx < x1; xx++)
				row[xx] = BlendPx(row[xx], color, alpha);
	}
}

void VGradient(const Target& t, int x, int y, int w, int h, uint32_t top, uint32_t bottom, int alpha)
{
	for (int j = 0; j < h; j++)
		Fill(t, x, y + j, w, 1, Mix(top, bottom, h > 1 ? j * 256 / (h - 1) : 0), alpha);
}

void HGradient(const Target& t, int x, int y, int w, int h, uint32_t left, uint32_t right, int alpha)
{
	for (int i = 0; i < w; i++)
		Fill(t, x + i, y, 1, h, Mix(left, right, w > 1 ? i * 256 / (w - 1) : 0), alpha);
}

void RoundRect(const Target& t, int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom, int alpha)
{
	if (w <= 0 || h <= 0)
		return;
	r = std::min(r, std::min(w, h) / 2);
	const float fx0 = float(x), fy0 = float(y), fx1 = float(x + w), fy1 = float(y + h);
	for (int j = 0; j < h; j++)
	{
		const int yy = y + j;
		if (yy < 0 || yy >= t.h)
			continue;
		const uint32_t c = top == bottom ? top : Mix(top, bottom, h > 1 ? j * 256 / (h - 1) : 0);
		const bool corner_row = j < r || j >= h - r;
		uint32_t* row = t.px + size_t(yy) * t.pitch;
		const int x0 = std::max(0, x), x1 = std::min(t.w, x + w);
		for (int xx = x0; xx < x1; xx++)
		{
			int a = alpha;
			if (corner_row && (xx < x + r || xx >= x + w - r))
				a = int(alpha * RoundCoverage(xx + 0.5f, yy + 0.5f, fx0, fy0, fx1, fy1, float(r)));
			row[xx] = BlendPx(row[xx], c, a);
		}
	}
}

void RoundRectOutline(const Target& t, int x, int y, int w, int h, int r, int thick, uint32_t color, int alpha)
{
	if (w <= 0 || h <= 0)
		return;
	r = std::min(r, std::min(w, h) / 2);
	const float fx0 = float(x), fy0 = float(y), fx1 = float(x + w), fy1 = float(y + h);
	const float ir = std::max(0.0f, float(r - thick));
	for (int j = 0; j < h; j++)
	{
		const int yy = y + j;
		if (yy < 0 || yy >= t.h)
			continue;
		const bool edge_row = j < std::max(r, thick) + 1 || j >= h - std::max(r, thick) - 1;
		uint32_t* row = t.px + size_t(yy) * t.pitch;
		for (int xx = std::max(0, x); xx < std::min(t.w, x + w); xx++)
		{
			if (!edge_row && xx >= x + std::max(r, thick) + 1 && xx < x + w - std::max(r, thick) - 1)
			{
				xx = x + w - std::max(r, thick) - 2; // skip the inside
				continue;
			}
			const float outer = RoundCoverage(xx + 0.5f, yy + 0.5f, fx0, fy0, fx1, fy1, float(r));
			const float inner = RoundCoverage(xx + 0.5f, yy + 0.5f, fx0 + thick, fy0 + thick, fx1 - thick, fy1 - thick, ir);
			const float cov = std::clamp(outer - inner, 0.0f, 1.0f);
			if (cov > 0)
				row[xx] = BlendPx(row[xx], color, int(alpha * cov));
		}
	}
}

void Circle(const Target& t, int cx, int cy, int r, uint32_t color, int alpha)
{
	for (int y = cy - r - 1; y <= cy + r + 1; y++)
		for (int x = cx - r - 1; x <= cx + r + 1; x++)
		{
			const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			const float cov = std::clamp(r + 0.5f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
			if (cov > 0)
				Put(t, x, y, color, int(alpha * cov));
		}
}

void Ring(const Target& t, int cx, int cy, int r, int thick, uint32_t color, int alpha)
{
	for (int y = cy - r - 1; y <= cy + r + 1; y++)
		for (int x = cx - r - 1; x <= cx + r + 1; x++)
		{
			const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			const float d = std::sqrt(dx * dx + dy * dy);
			const float cov = std::clamp(r + 0.5f - d, 0.0f, 1.0f) - std::clamp(r - thick + 0.5f - d, 0.0f, 1.0f);
			if (cov > 0)
				Put(t, x, y, color, int(alpha * cov));
		}
}

void SoftEllipse(const Target& t, int cx, int cy, int rx, int ry, uint32_t color, int alpha)
{
	if (rx <= 0 || ry <= 0)
		return;
	for (int y = std::max(0, cy - ry); y < std::min(t.h, cy + ry); y++)
	{
		uint32_t* row = t.px + size_t(y) * t.pitch;
		const float ny = (y + 0.5f - cy) / ry;
		for (int x = std::max(0, cx - rx); x < std::min(t.w, cx + rx); x++)
		{
			const float nx = (x + 0.5f - cx) / rx;
			const float d = nx * nx + ny * ny;
			if (d >= 1.0f)
				continue;
			const float k = d < 0.55f ? 1.0f : 1.0f - (d - 0.55f) / 0.45f; // a solid middle and a soft rim
			row[x] = BlendPx(row[x], color, int(alpha * k));
		}
	}
}

void Triangle(const Target& t, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color, int alpha)
{
	const int minx = std::max(0, std::min({x0, x1, x2})), maxx = std::min(t.w - 1, std::max({x0, x1, x2}));
	const int miny = std::max(0, std::min({y0, y1, y2})), maxy = std::min(t.h - 1, std::max({y0, y1, y2}));
	auto edge = [](float ax, float ay, float bx, float by, float px, float py) {
		return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
	};
	const float area = edge(float(x0), float(y0), float(x1), float(y1), float(x2), float(y2));
	if (std::fabs(area) < 1)
		return;
	for (int y = miny; y <= maxy; y++)
		for (int x = minx; x <= maxx; x++)
		{
			int inside = 0;
			for (int s = 0; s < 4; s++) // 4 samples a pixel: smooth edges
			{
				const float px = x + 0.25f + 0.5f * (s & 1), py = y + 0.25f + 0.5f * (s >> 1);
				const float w0 = edge(float(x1), float(y1), float(x2), float(y2), px, py);
				const float w1 = edge(float(x2), float(y2), float(x0), float(y0), px, py);
				const float w2 = edge(float(x0), float(y0), float(x1), float(y1), px, py);
				if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
					inside++;
			}
			if (inside)
				Put(t, x, y, color, alpha * inside / 4);
		}
}

void FitRect(const CoverTex& tex, int x, int y, int w, int h, int* ox, int* oy, int* ow, int* oh)
{
	const double aspect = tex.h[0] > 0 ? double(tex.w[0]) / tex.h[0] : 0.7;
	int fw = w, fh = int(w / aspect);
	if (fh > h)
	{
		fh = h;
		fw = int(h * aspect);
	}
	*ox = x + (w - fw) / 2;
	*oy = y + (h - fh) / 2;
	*ow = fw;
	*oh = fh;
}

void Cover(const Target& t, const CoverTex& tex, int x, int y, int w, int h, int alpha, int shade_percent)
{
	if (w <= 0 || h <= 0)
		return;
	int l = 0;
	while (l + 1 < CoverTex::kLevels && tex.h[l + 1] >= h && !tex.px[l + 1].empty())
		l++;
	const int tw = tex.w[l], th = tex.h[l];
	if (tw <= 0 || th <= 0 || tex.px[l].empty())
		return;
	const uint32_t* src = tex.px[l].data(); // column by column: src[x * th + y]
	const int sh = shade_percent;
	for (int j = 0; j < h; j++)
	{
		const int yy = y + j;
		if (yy < 0 || yy >= t.h)
			continue;
		const float sy = std::clamp((j + 0.5f) * th / h - 0.5f, 0.0f, float(th - 1));
		const int y0 = int(sy), y1 = std::min(th - 1, y0 + 1);
		const int fy = int((sy - y0) * 256);
		uint32_t* row = t.px + size_t(yy) * t.pitch;
		for (int i = 0; i < w; i++)
		{
			const int xx = x + i;
			if (xx < 0 || xx >= t.w)
				continue;
			const float sx = std::clamp((i + 0.5f) * tw / w - 0.5f, 0.0f, float(tw - 1));
			const int x0 = int(sx), x1 = std::min(tw - 1, x0 + 1);
			const int fx = int((sx - x0) * 256);
			const uint32_t a = Mix(src[size_t(x0) * th + y0], src[size_t(x1) * th + y0], fx);
			const uint32_t b = Mix(src[size_t(x0) * th + y1], src[size_t(x1) * th + y1], fx);
			uint32_t c = Mix(a, b, fy);
			if (sh != 100)
				c = Shade(c, sh);
			row[xx] = BlendPx(row[xx], c, alpha);
		}
	}
}

Target Canvas::target()
{
	Allocate();
	return Target{px.data(), ps5video::kWidth, ps5video::kHeight, ps5video::kWidth};
}

void Canvas::Allocate()
{
	if (px.empty())
		px.assign(size_t(ps5video::kWidth) * ps5video::kHeight, 0xff000000u);
}

void Canvas::CopyToScreen() const
{
	uint32_t* s = ps5video::Surface();
	if (s && !px.empty())
		memcpy(s, px.data(), px.size() * 4);
}
} // namespace draw
} // namespace fe
