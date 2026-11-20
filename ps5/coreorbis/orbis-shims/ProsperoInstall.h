// fceuxs5: installs the dashboard app (/data/homebrew/<TITLE_ID>/) from files built into the payload.
//
// PS5SX2 gets its app folder from its installer, which downloads a release zip from GitHub and unpacks
// PPSA99203/ into /data/homebrew/. fceuxs5 carries that folder inside fceuxs5.elf, its installer, instead
// (eboot.bin, sce_module/libc.prx, sce_sys/param.json, icon0.png, pic0.dds, pic1.dds -- see install_data.cpp), and
// writes it every time it is sent when what is on the console differs. ShadowMountPlus then puts the icon
// on the home screen, as for PS5SX2.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <string>

#ifndef FCEUXS5_TITLE_ID
#define FCEUXS5_TITLE_ID "PPSA51855"
#endif

struct EmbeddedAppFile
{
	const char* rel; // path inside the app folder, e.g. "sce_sys/icon0.png"
	const unsigned char* begin;
	const unsigned char* end;
};

// The files built into this payload (nullptr-terminated list), or nullptr when the build has none.
const EmbeddedAppFile* EmbeddedAppFiles();

enum class InstallResult
{
	NothingEmbedded,
	UpToDate,
	Installed, // there was no app before
	Updated,
	Failed,
	NewerInstalled, // the app folder holds a newer version (updated from the app): left as it is
	CantWrite, // the console runs the app from an image or a read-only folder (AppInstallPlace().reason)
};
// The installed app's version (its param.json's contentVersion), "" when there is none.
std::string InstalledVersion();

// Where the app goes (1.2): the folder the console runs it from when ShadowMount+ mounted one (written through its
// mount, ProsperoAppPlace.h), else /data/homebrew/<TITLE_ID> (FCEUXS5_PS5_HOMEBREW overrides /data/homebrew for the
// host tests).
std::string AppInstallDir();
struct AppPlace;
const AppPlace& AppInstallPlace();
InstallResult InstallApp();

// /user/appmeta/<TITLE_ID> (FCEUXS5_PS5_APPMETA overrides /user/appmeta for the host tests): where the home screen
// reads the icon and the backgrounds. Brings our art there up to date; the number of files changed.
std::string AppMetaDir();
int SyncAppMeta();
