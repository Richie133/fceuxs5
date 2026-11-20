// fceuxs5: force-included into every FCEUX core file (CORE_FORCE in ps5/Makefile).
//
// FCEUX's core expects the platform driver to declare a few things through its own headers (the Qt driver's
// drivers/Qt/sdl.h, the Windows driver's drivers/win/main.h). The PS5 driver (fceux_driver.cpp) has none, so the
// declarations those headers would bring in are made here once.
//
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "driver.h"

extern int pal_emulation; // 1: PAL (the region the driver asked for, or the game's)
extern int dendy;         // 1: Dendy timing
