// fceuxs5: the /data/fceuxs5 folder layout and the boot log (orbis-shims/orbis_paths.cpp).
//
//   /data/fceuxs5/roms         games (NES/ .nes .unf .unif, FDS/ .fds, also in .zip); sub-folders are fine
//   /data/fceuxs5/bios         the Famicom Disk System BIOS (disksys.rom), HD packs (HdPacks/<game>/hires.txt)
//   /data/fceuxs5/saves        battery saves and disk changes (<system>/<game>.srm)
//   /data/fceuxs5/states       save states, <game>.state1 .. <game>.state10
//   /data/fceuxs5/covers       box art (downloaded, or your own <ROM file name>.png), crc-cache.txt, wanted.txt
//   /data/fceuxs5/logs         boot.log (this run) and boot.prev.log (the run before); installer.log, helper.log
//   /data/fceuxs5/fceuxs5.ini  the frontend's settings
//
// USB drives are searched too: /mnt/usbN/fceuxs5/roms (N = 0..7) and /mnt/extN/fceuxs5/roms.
//
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

#ifndef ORBIS_ROOT_DEFAULT
#define ORBIS_ROOT_DEFAULT "/data/fceuxs5"
#endif

// Root folder (ORBIS_ROOT_DEFAULT; the host tests point it elsewhere with FCEUXS5_PS5_ROOT).
const std::string& OrbisRoot();
// <root>/<sub>, created at boot by OrbisPathsInit.
std::string OrbisDir(const char* sub);
// Creates the folder tree. Returns false when the root itself can't be created (no /data access).
bool OrbisPathsInit();
// Folders that hold ROMs and exist right now (internal first, then USB drives).
std::vector<std::string> OrbisRomRoots();

bool OrbisIsDir(const std::string& path);
bool OrbisIsFile(const std::string& path);
bool OrbisMkdirs(const std::string& path);

// <root>/logs/<name>.log (the run before kept as <name>.prev.log): boot.log for the emulator, installer.log
// for the installer/helper payload. Every line also goes to stdout (once the log is open). Lines logged before the file is open
// (before the jailbreak shows /data to the app) are kept and written first.
void OrbisLogOpen(const char* name = "boot");
void OrbisLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void OrbisLogClose();
// Debug logs on or off: the "Debug logs" setting (debug_logs= in fceuxs5.ini, on unless it says 0).
// OrbisLogOpen reads it, so the app, the installer and the helper all follow it; while off, OrbisLog writes
// nothing (no file, no stdout) and the log files of earlier runs are left as they are.
void OrbisLogSetEnabled(bool on);
// Reads the setting again and applies it (the helper, which keeps running, calls this for each request).
void OrbisLogRefresh();
bool OrbisLogEnabled();
// boot.log onto the disk now (fflush + fsync): before a step that might take the process down (the exceptions
// self-test), so the last line written says where it was (1.2)
void OrbisLogSync();
// The open log's file descriptor, for the crash handler's signal-safe write(); -1 before it is open.
int OrbisLogFd();
