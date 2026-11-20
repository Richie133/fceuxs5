// fceuxs5 frontend: ROM hacks -- IPS, UPS and BPS patches applied in memory when a game starts ("soft-patching",
// as RetroArch does it): the ROM file on the drive is never changed.
//
// Where patches are found for a game "Game.nes" (or "Game.zip" / "Game.7z"):
//   1. beside it, with the same name: Game.ips / Game.ups / Game.bps (applied automatically when "Auto-apply
//      patches" is on, the default);
//   2. beside it, starting with its name: "Game (Translation).ips", "Game - Hard Mode.bps";
//   3. in /data/fceuxs5/patches/: "Game.bps", "Game (anything).ips", and every patch in a folder named after the
//      game (/data/fceuxs5/patches/Game/*.ips).
// The game's options (Square in the library) list them all; the one chosen is remembered for that game
// (/data/fceuxs5/patches.txt), "Original game" turns them off.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fe
{
enum class PatchFormat
{
	Unknown,
	Ips,
	Ups,
	Bps,
};

PatchFormat PatchFormatOf(const std::string& path); // by extension
bool IsPatchExt(const std::string& ext_lower);       // ".ips", ".ups", ".bps"

// Applies patch to rom (a whole .nes / .fds / .unf file as read). On success rom holds the patched game.
// *note: a sentence for the log and the screen ("BPS: source checksum differs; applied anyway"), may be empty.
// UPS and BPS check the source's CRC32: a patch made for the ROM without its 16-byte iNES header is applied to the
// body and the header put back; when neither matches, the patch is applied all the same with a warning (many
// hacks are distributed against a slightly different dump).
bool ApplyPatch(const std::string& patch_path, std::vector<uint8_t>& rom, std::string* note, std::string* error);
bool ApplyPatchData(PatchFormat fmt, const std::vector<uint8_t>& patch, std::vector<uint8_t>& rom, std::string* note,
	std::string* error);

// Every patch that belongs to the game at rom_path (see above), the automatic same-name one first.
std::vector<std::string> FindPatches(const std::string& rom_path);
// The patch to use for this game: the remembered choice; else the same-name patch when auto-apply is on; else "".
// "" also when the player chose "Original game".
std::string PatchForGame(const std::string& rom_path);
// Remembers the choice for the game ("" = the original game, "auto" = forget the choice).
void SetPatchForGame(const std::string& rom_path, const std::string& patch_path);
// The remembered choice for the game, if there is one (*patch: "" = the original game, else the patch's path).
bool GetPatchChoice(const std::string& rom_path, std::string* patch);
// The patch's file name without folder and extension ("Super Mario Bros. 3 (Mix)").
std::string PatchLabel(const std::string& patch_path);
} // namespace fe
