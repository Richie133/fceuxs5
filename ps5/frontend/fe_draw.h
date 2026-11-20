// fceuxs5 frontend: 2D drawing for the interface themes -- gradients, anti-aliased rounded rectangles and circles,
// alpha blending and scaled box art -- on the CPU, into the 1920x1080 A8B8G8R8 surface (ProsperoVideo.h) or any
// buffer of the same layout.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace fe
{
struct CoverTex;

namespace draw
{
struct Target
{
	uint32_t* px;
	int w, h, pitch;
};
Target Screen(); // the video surface

// colours are ps5video::Rgb() values (0xAABBGGRR); alpha 0..255
uint32_t Mix(uint32_t a, uint32_t b, int t);       // t 0..256: a -> b
uint32_t Shade(uint32_t c, int percent);           // 100 = itself, < 100 darker, > 100 lighter
void Fill(const Target& t, int x, int y, int w, int h, uint32_t color, int alpha = 255);
void VGradient(const Target& t, int x, int y, int w, int h, uint32_t top, uint32_t bottom, int alpha = 255);
void HGradient(const Target& t, int x, int y, int w, int h, uint32_t left, uint32_t right, int alpha = 255);
// rounded rectangles: filled (a vertical gradient when top != bottom), and an outline `thick` pixels wide
void RoundRect(const Target& t, int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom, int alpha = 255);
void RoundRectOutline(const Target& t, int x, int y, int w, int h, int r, int thick, uint32_t color, int alpha = 255);
void Circle(const Target& t, int cx, int cy, int r, uint32_t color, int alpha = 255);
void Ring(const Target& t, int cx, int cy, int r, int thick, uint32_t color, int alpha = 255);
// a soft blob (clouds, glows): alpha fades out towards the edge of the ellipse
void SoftEllipse(const Target& t, int cx, int cy, int rx, int ry, uint32_t color, int alpha = 255);
void Triangle(const Target& t, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color, int alpha = 255);

// box art into (x, y, w, h), bilinear, from the level that fits; shade_percent darkens / lightens it
void Cover(const Target& t, const CoverTex& tex, int x, int y, int w, int h, int alpha = 255, int shade_percent = 100);
// the largest rectangle of the cover's aspect inside (x, y, w, h), centred
void FitRect(const CoverTex& tex, int x, int y, int w, int h, int* ox, int* oy, int* ow, int* oh);

// A full-screen picture kept between frames (a theme's background), restored with one copy.
struct Canvas
{
	std::vector<uint32_t> px;
	int key = -1; // what was drawn into it (the theme), -1 nothing
	Target target();
	void Allocate();
	void CopyToScreen() const;
};
} // namespace draw
} // namespace fe
