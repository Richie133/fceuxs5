// fceuxs5 frontend: the intro video and the menu music (fe_media.h).
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_media.h"

#include "OrbisPaths.h"
#include "ProsperoAudio.h"
#include "ProsperoInput.h"
#include "ProsperoSce.h"
#include "ProsperoThread.h"
#include "ProsperoVideo.h"
#include "fe_settings.h"
#include "fe_text.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <unistd.h>
#include <vector>

#define PL_MPEG_IMPLEMENTATION
#include "third_party/pl_mpeg.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "third_party/dr_mp3.h"

#define FCEUXS5_INCBIN(sym, path)                                                                                  \
	__asm__(".section .rodata\n"                                                                               \
			".balign 16\n"                                                                                     \
			".global " #sym "_begin\n" #sym "_begin:\n"                                                        \
			".incbin \"" path "\"\n"                                                                           \
			".global " #sym "_end\n" #sym "_end:\n"                                                            \
			".previous\n");                                                                                    \
	extern "C" const unsigned char sym##_begin[];                                                              \
	extern "C" const unsigned char sym##_end[];

FCEUXS5_INCBIN(fceuxs5_intro, FE_MEDIA_DIR "/intro.mpg")
FCEUXS5_INCBIN(fceuxs5_music, FE_MEDIA_DIR "/music.mp3")

namespace fe
{
namespace
{
double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

inline int16_t ToS16(float f)
{
	const int v = int(std::lround(f * 32767.0f));
	return int16_t(std::max(-32768, std::min(32767, v)));
}

// ---- intro --------------------------------------------------------------------------------------------------------
struct IntroState
{
	std::vector<uint8_t> rgba;
	int w = 0, h = 0;
	bool new_frame = false;
	double src_rate = 44100;
	double frac = 0;
	int16_t prev_l = 0, prev_r = 0;
	std::vector<int16_t> out;
	bool sound = true;
};

void OnVideo(plm_t*, plm_frame_t* frame, void* user)
{
	IntroState* s = static_cast<IntroState*>(user);
	s->w = int(frame->width);
	s->h = int(frame->height);
	s->rgba.resize(size_t(s->w) * s->h * 4);
	plm_frame_to_rgba(frame, s->rgba.data(), s->w * 4);
	s->new_frame = true;
}

void OnAudio(plm_t*, plm_samples_t* samples, void* user)
{
	IntroState* s = static_cast<IntroState*>(user);
	if (!s->sound || !ps5audio::Available())
		return;
	// 44.1 kHz -> 48 kHz, linear
	const int n = int(samples->count);
	const double step = s->src_rate / double(ps5audio::kRate);
	s->out.clear();
	double pos = s->frac;
	while (pos < n)
	{
		const int i = int(pos);
		const float t = float(pos - i);
		const float l0 = i == 0 ? s->prev_l / 32767.0f : samples->interleaved[(i - 1) * 2];
		const float r0 = i == 0 ? s->prev_r / 32767.0f : samples->interleaved[(i - 1) * 2 + 1];
		const float l1 = samples->interleaved[i * 2];
		const float r1 = samples->interleaved[i * 2 + 1];
		s->out.push_back(ToS16(l0 + (l1 - l0) * t));
		s->out.push_back(ToS16(r0 + (r1 - r0) * t));
		pos += step;
	}
	s->frac = pos - n;
	s->prev_l = ToS16(samples->interleaved[(n - 1) * 2]);
	s->prev_r = ToS16(samples->interleaved[(n - 1) * 2 + 1]);
	// the ring holds ~340 ms; wait for room rather than drop (the video waits with it)
	const double give_up = Now() + 0.25;
	int done = 0;
	const int frames = int(s->out.size() / 2);
	while (done < frames && Now() < give_up)
	{
		const int put = ps5audio::Push(s->out.data() + done * 2, std::min(frames - done, ps5audio::Free()));
		done += put;
		if (done < frames)
			usleep(2000);
	}
}

// Draws the frame scaled by the largest whole factor that fits (the cracktro's pixels stay sharp), centred.
void DrawIntroFrame(const IntroState& s)
{
	uint32_t* surf = ps5video::Surface();
	if (!surf || s.w <= 0 || s.h <= 0)
		return;
	const int W = ps5video::kWidth, H = ps5video::kHeight;
	int k = std::max(1, std::min(W / s.w, H / s.h));
	const int dw = s.w * k, dh = s.h * k;
	const int ox = (W - dw) / 2, oy = (H - dh) / 2;
	const uint32_t* src = reinterpret_cast<const uint32_t*>(s.rgba.data());
	for (int y = 0; y < dh; y++)
	{
		const uint32_t* sr = src + size_t(y / k) * s.w;
		uint32_t* dr = surf + size_t(oy + y) * W + ox;
		for (int x = 0; x < dw; x++)
			dr[x] = sr[x / k] | 0xff000000u;
	}
}

// ---- music ---------------------------------------------------------------------------------------------------------
struct Music
{
	std::mutex lock;
	std::condition_variable wake;
	bool want = false;      // play
	bool quit = false;
	bool writing = false;   // the thread is between its checks and its Push
	std::atomic<int> volume{70};
	std::unique_ptr<ps5::BigThread> thread;
	drmp3 mp3 = {};
	bool opened = false;
};

Music& M()
{
	static Music* m = new Music();
	return *m;
}

void MusicLoop()
{
	Music& m = M();
	if (!drmp3_init_memory(&m.mp3, fceuxs5_music_begin, size_t(fceuxs5_music_end - fceuxs5_music_begin), nullptr))
	{
		OrbisLog("[music] the built-in MP3 can't be decoded");
		return;
	}
	m.opened = true;
	const unsigned channels = m.mp3.channels ? m.mp3.channels : 2;
	const double src_rate = m.mp3.sampleRate ? m.mp3.sampleRate : 48000;
	OrbisLog("[music] %u Hz, %u channel(s)", unsigned(src_rate), channels);
	std::vector<int16_t> in(1152 * 2 * 2), out;
	double frac = 0;
	int16_t prev_l = 0, prev_r = 0;
	constexpr int kAhead = ps5audio::kRate / 8; // keep 125 ms queued
	for (;;)
	{
		{
			std::unique_lock<std::mutex> l(m.lock);
			m.writing = false;
			m.wake.notify_all();
			m.wake.wait(l, [&] { return m.quit || m.want; });
			if (m.quit)
				break;
			m.writing = true;
		}
		if (!ps5audio::Available() || ps5audio::Queued() > kAhead)
		{
			usleep(5000);
			continue;
		}
		const drmp3_uint64 got = drmp3_read_pcm_frames_s16(&m.mp3, 1152, in.data());
		if (got == 0)
		{
			drmp3_seek_to_pcm_frame(&m.mp3, 0); // loop
			continue;
		}
		const int vol = m.volume.load() * 256 / 100;
		out.clear();
		auto sample = [&](drmp3_uint64 i, int ch) -> int {
			return channels == 1 ? in[size_t(i)] : in[size_t(i) * channels + size_t(ch)];
		};
		if (std::fabs(src_rate - ps5audio::kRate) < 1)
		{
			for (drmp3_uint64 i = 0; i < got; i++)
			{
				out.push_back(int16_t((sample(i, 0) * vol) >> 8));
				out.push_back(int16_t((sample(i, 1 % channels) * vol) >> 8));
			}
		}
		else
		{
			const double step = src_rate / ps5audio::kRate;
			double pos = frac;
			while (pos < double(got))
			{
				const int i = int(pos);
				const int t = int((pos - i) * 256);
				const int l0 = i == 0 ? prev_l : sample(i - 1, 0), r0 = i == 0 ? prev_r : sample(i - 1, 1 % channels);
				const int l1 = sample(i, 0), r1 = sample(i, 1 % channels);
				out.push_back(int16_t((((l0 * (256 - t) + l1 * t) >> 8) * vol) >> 8));
				out.push_back(int16_t((((r0 * (256 - t) + r1 * t) >> 8) * vol) >> 8));
				pos += step;
			}
			frac = pos - double(got);
			prev_l = int16_t(sample(got - 1, 0));
			prev_r = int16_t(sample(got - 1, 1 % channels));
		}
		int done = 0;
		const int frames = int(out.size() / 2);
		while (done < frames)
		{
			{
				std::lock_guard<std::mutex> l(m.lock);
				if (!m.want || m.quit)
					break;
			}
			done += ps5audio::Push(out.data() + done * 2, std::min(frames - done, ps5audio::Free()));
			if (done < frames)
				usleep(3000);
		}
	}
	drmp3_uninit(&m.mp3);
	m.opened = false;
}
} // namespace

void PlayIntro()
{
	FCEUXS5_STAGE(Boot, "intro");
	const size_t size = size_t(fceuxs5_intro_end - fceuxs5_intro_begin);
	plm_t* plm = plm_create_with_memory(const_cast<uint8_t*>(fceuxs5_intro_begin), size, 0);
	if (!plm)
	{
		OrbisLog("[intro] can't open the built-in video");
		return;
	}
	IntroState st;
	st.sound = Config().audio;
	plm_set_loop(plm, 0);
	plm_set_audio_enabled(plm, st.sound ? 1 : 0);
	if (plm_get_num_audio_streams(plm) > 0)
	{
		st.src_rate = plm_get_samplerate(plm) > 0 ? plm_get_samplerate(plm) : 44100;
		plm_set_audio_lead_time(plm, 0.08);
	}
	plm_set_video_decode_callback(plm, OnVideo, &st);
	plm_set_audio_decode_callback(plm, OnAudio, &st);
	OrbisLog("[intro] %dx%d, %.2f fps, %.1f s", plm_get_width(plm), plm_get_height(plm), plm_get_framerate(plm),
		plm_get_duration(plm));

	ps5video::FillRect(0, 0, ps5video::kWidth, ps5video::kHeight, ps5video::Rgb(0, 0, 0));
	ps5video::Present(0, 0, 0, 0, true);
	// buttons already held when it starts (the one that opened the app) don't skip it
	ps5input::Poll();
	uint32_t held = ps5input::Pad(0).raw_buttons;
	double last = Now();
	const double started = last;
	bool skip_hint = false;
	while (!plm_has_ended(plm))
	{
		ps5input::Poll();
		const uint32_t raw = ps5input::Pad(0).raw_buttons;
		const uint32_t pressed = raw & ~held;
		held = raw;
		if (pressed & (SCE_PAD_BUTTON_CROSS | SCE_PAD_BUTTON_CIRCLE | SCE_PAD_BUTTON_OPTIONS))
		{
			OrbisLog("[intro] skipped");
			break;
		}
		const double now = Now();
		double dt = now - last;
		last = now;
		if (dt > 0.1)
			dt = 0.1;
		plm_decode(plm, dt);
		const bool hint = now - started > 1.0;
		if (st.new_frame || hint != skip_hint)
		{
			st.new_frame = false;
			skip_hint = hint;
			DrawIntroFrame(st);
			if (hint)
			{
				const char* text = "\xE2\x87\xA3  Skip"; // Cross
				const int w = TextWidthPx(text, 30);
				ps5video::FillRect(ps5video::kWidth - w - 80, ps5video::kHeight - 90, w + 40, 50, ps5video::Rgb(0, 0, 0));
				DrawTextPx(ps5video::kWidth - w - 60, ps5video::kHeight - 84, text, 30, ps5video::Rgb(200, 200, 200));
			}
			ps5video::Present(0, 0, 0, 0, true);
		}
		else
			usleep(4000);
	}
	plm_destroy(plm);
	ps5video::FillRect(0, 0, ps5video::kWidth, ps5video::kHeight, ps5video::Rgb(0, 0, 0));
	ps5video::Present(0, 0, 0, 0, false);
	ps5video::InvalidateFrame();
	OrbisLog("[intro] done");
}

void MusicStart()
{
	Music& m = M();
	m.volume = Config().music_volume;
	if (!Config().music || !Config().audio)
		return;
	if (!m.thread)
	{
		m.thread = std::make_unique<ps5::BigThread>(MusicLoop, 512 * 1024);
		if (!m.thread->joinable())
		{
			m.thread.reset();
			OrbisLog("[music] no thread: no music");
			return;
		}
	}
	std::lock_guard<std::mutex> l(m.lock);
	m.want = true;
	m.wake.notify_all();
}

void MusicStop()
{
	Music& m = M();
	if (!m.thread)
		return;
	std::unique_lock<std::mutex> l(m.lock);
	m.want = false;
	m.wake.notify_all();
	m.wake.wait_for(l, std::chrono::milliseconds(500), [&] { return !m.writing; });
}

void MusicShutdown()
{
	Music& m = M();
	if (!m.thread)
		return;
	{
		std::lock_guard<std::mutex> l(m.lock);
		m.quit = true;
		m.want = false;
		m.wake.notify_all();
	}
	m.thread->join();
	m.thread.reset();
}

void MusicApplySettings()
{
	M().volume = Config().music_volume;
	if (Config().music && Config().audio)
		MusicStart();
	else
		MusicStop();
}

bool MusicPlaying()
{
	Music& m = M();
	std::lock_guard<std::mutex> l(m.lock);
	return m.thread && m.want;
}
} // namespace fe
