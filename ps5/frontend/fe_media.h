// fceuxs5 frontend: the intro video and the menu music.
//
// Both are built into the eboot (assets/media/): the intro is an MPEG-1 video with MP2 sound decoded with pl_mpeg
// (Dominic Szablewski, MIT), the music an MP3 decoded with dr_mp3 (David Reid, public domain / MIT-0).
//
// The music plays on a thread of its own while the launcher and its menus are up, looping, at the "Menu music"
// volume; it stops (and the audio ring is left to the emulator) while a game runs.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace fe
{
// Plays the intro from the start; returns when it ends or the player presses Cross, Circle or OPTIONS.
void PlayIntro();

void MusicStart();      // starts or resumes (no-op when the setting is off)
void MusicStop();       // stops and waits until the thread no longer writes to the audio ring
void MusicShutdown();   // ends the thread (the app is closing)
void MusicApplySettings(); // the "Menu music" switch and volume changed
bool MusicPlaying();
} // namespace fe
