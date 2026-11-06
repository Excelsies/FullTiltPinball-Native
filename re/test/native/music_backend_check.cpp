// Check of the fluidsynth music backend (midi.cpp + music_fluidsynth.cpp) through midi's public API,
// with SDL's dummy audio driver (nothing audible). Built and run by music_backend_check.sh, which links
// the hook build's objects with --wrap on the fluidsynth and SDL_mixer calls observed below.
//
// 1. Track files: an empty, a too short, a non-MIDI .MID and a non-MIDS .MDS give load_track 0, as
//    with SDL_mixer (Mix_LoadMUS_RW returns no music for them); a Standard MIDI File gives a handle.
// 2. Threads: while the music hook is attached, the main thread makes no synthesizer call (volume
//    changes from the main thread reach the synthesizer through the audio thread).
// 3. Restarts: after a restart, no voice of the stopped play is still sounding once the first audio
//    callback of the new play has rendered.
// 4. Volume while stopped: volume changes while nothing plays make no synthesizer call (the stopped
//    play's voices can still be listed, and fluidsynth would queue a gain change for each), and the
//    next play applies the last one.
// Exit 0 = pass, 1 = fail, 2 = cannot run.

#include "midi.h"
#include "options.h"
#include "pb.h"

#include <fluidsynth.h>

#include <atomic>
#include <chrono>
#include <thread>

extern "C" {
fluid_synth_t* __real_new_fluid_synth(fluid_settings_t* settings);
void __real_fluid_synth_set_gain(fluid_synth_t* synth, float gain);
int __real_fluid_synth_write_s16(fluid_synth_t* synth, int len, void* lout, int loff, int lincr, void* rout,
                                 int roff, int rincr);
int __real_fluid_synth_system_reset(fluid_synth_t* synth);
int __real_fluid_player_play(fluid_player_t* player);
void __real_Mix_HookMusic(void (SDLCALL *mix_func)(void*, Uint8*, int), void* arg);
}

namespace
{
	fluid_synth_t* Synth = nullptr;
	std::thread::id MainThread;
	std::atomic<bool> Attached{false};
	std::atomic<int> MainCallsWhileAttached{0}, GainCalls{0};
	// Restart observation: voices with an id below Boundary belong to plays before the latest one.
	std::atomic<unsigned> Boundary{0};
	std::atomic<int> CallbacksSincePlay{0}, OldVoiceCallbacks{0}, OldVoicesMax{0};

	void MainThreadCall()
	{
		if (std::this_thread::get_id() == MainThread && Attached.load())
			MainCallsWhileAttached++;
	}

	/// Voices listed by the synthesizer (fluid_synth_get_voicelist: playing or released, not finished).
	int ListedVoices(fluid_voice_t** list, int size)
	{
		for (int i = 0; i < size; i++)
			list[i] = nullptr;
		fluid_synth_get_voicelist(Synth, list, size, -1);
		int n = 0;
		while (n < size && list[n])
			n++;
		return n;
	}

	void WriteFile(const std::string& path, const std::vector<uint8_t>& bytes)
	{
		auto file = fopen(path.c_str(), "wb");
		if (!file || fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size())
		{
			printf("cannot write %s\n", path.c_str());
			exit(2);
		}
		fclose(file);
	}

	/// Format 0 MIDI, 96 ticks per quarter at 120 bpm (96 ticks = 0.5 s): strings chord on channel 0 and
	/// drums on channel 9 from tick 0, drums off at 1 s, chord off at 2 s, end at about 2.1 s.
	std::vector<uint8_t> TestMidi()
	{
		const uint8_t events[] = {
			0x00, 0xC0, 48, // program: strings (long release)
			0x00, 0x90, 60, 100, 0x00, 0x90, 64, 100, 0x00, 0x90, 67, 100,
			0x00, 0x99, 37, 100, 0x00, 0x99, 42, 100,
			0x81, 0x40, 0x89, 37, 0, 0x00, 0x89, 42, 0, // delta 192
			0x81, 0x40, 0x80, 60, 0, 0x00, 0x80, 64, 0, 0x00, 0x80, 67, 0,
			0x10, 0xFF, 0x2F, 0x00,
		};
		std::vector<uint8_t> midi = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k', 0, 0, 0,
		                             static_cast<uint8_t>(sizeof events)};
		midi.insert(midi.end(), events, events + sizeof events);
		return midi;
	}

	int Failures = 0;

	void Expect(bool ok, const char* what)
	{
		printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
		if (!ok)
			Failures++;
	}
}

extern "C" {
fluid_synth_t* __wrap_new_fluid_synth(fluid_settings_t* settings)
{
	Synth = __real_new_fluid_synth(settings);
	return Synth;
}

void __wrap_fluid_synth_set_gain(fluid_synth_t* synth, float gain)
{
	MainThreadCall();
	GainCalls++;
	__real_fluid_synth_set_gain(synth, gain);
}

int __wrap_fluid_synth_system_reset(fluid_synth_t* synth)
{
	MainThreadCall();
	return __real_fluid_synth_system_reset(synth);
}

int __wrap_fluid_player_play(fluid_player_t* player)
{
	MainThreadCall();
	// The hook is detached here: the voice list is stable. New voices get larger ids.
	fluid_voice_t* list[1024];
	unsigned next = 0;
	for (int i = 0, n = ListedVoices(list, 1024); i < n; i++)
		next = std::max(next, fluid_voice_get_id(list[i]) + 1);
	Boundary = next;
	CallbacksSincePlay = 0;
	return __real_fluid_player_play(player);
}

int __wrap_fluid_synth_write_s16(fluid_synth_t* synth, int len, void* lout, int loff, int lincr, void* rout,
                                 int roff, int rincr)
{
	auto result = __real_fluid_synth_write_s16(synth, len, lout, loff, lincr, rout, roff, rincr);
	// Audio thread, inside the music hook: count the earlier plays' voices still listed.
	fluid_voice_t* list[1024];
	int old = 0;
	for (int i = 0, n = ListedVoices(list, 1024); i < n; i++)
		if (fluid_voice_get_id(list[i]) < Boundary.load())
			old++;
	if (old)
	{
		OldVoiceCallbacks++;
		if (old > OldVoicesMax)
			OldVoicesMax = old;
	}
	CallbacksSincePlay++;
	return result;
}

void __wrap_Mix_HookMusic(void (SDLCALL *mix_func)(void*, Uint8*, int), void* arg)
{
	__real_Mix_HookMusic(mix_func, arg);
	Attached = mix_func != nullptr;
}
}

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		printf("usage: %s <soundfont.sf2> <work dir>\n", argv[0]);
		return 2;
	}
	MainThread = std::this_thread::get_id();
	setenv("SDL_AUDIODRIVER", "dummy", 1);
	setenv("SDL_AUDIO_DRIVER", "dummy", 1);
	setenv("SDL_SOUNDFONTS", argv[1], 1);
	std::string dir = argv[2];
	if (SDL_Init(SDL_INIT_AUDIO) != 0 || Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, MIX_DEFAULT_FORMAT, 2, 1024) != 0)
	{
		printf("cannot open audio: %s\n", SDL_GetError());
		return 2;
	}

	// Full Tilt layout: tracks in <BasePath>SOUND/, .MDS first, then .MID.
	pb::FullTiltMode = true;
	pb::BasePath = dir + "/";
	WriteFile(dir + "/SOUND/EMPTY.MID", {});
	WriteFile(dir + "/SOUND/SHORT.MID", {'M', 'T', 'h', 'd'});
	WriteFile(dir + "/SOUND/TEXT.MID", {'n', 'o', 't', ' ', 'a', ' ', 'm', 'i', 'd', 'i', ' ', 'f', 'i', 'l', 'e'});
	WriteFile(dir + "/SOUND/TEXT.MDS", {'n', 'o', 't', ' ', 'a', ' ', 'm', 'i', 'd', 's', ' ', 'f', 'i', 'l', 'e'});
	WriteFile(dir + "/SOUND/GOOD.MID", TestMidi());

	options::Options.Music.V = true;
	midi::music_init(true, MIX_MAX_VOLUME);
	if (!Synth)
	{
		printf("the fluidsynth backend is not in use (build without fluidsynth, or no SoundFont at %s)\n", argv[1]);
		return 2;
	}

	// 1. Track files
	for (auto name : {"EMPTY", "SHORT", "TEXT"})
	{
		auto handle = midi::load_track(name);
		Expect(handle == 0, (std::string(name) + ": load_track returns 0").c_str());
	}
	auto good = midi::load_track("GOOD");
	Expect(good > 0, "GOOD (Standard MIDI File): load_track returns a handle");
	if (good <= 0)
		return 1;

	// 2 and 3. Plays and restarts at different points of the track, with volume changes from the main
	// thread during playback.
	midi::music_play();
	MainCallsWhileAttached = 0;
	int restarts = 0, gainCallsBefore = GainCalls;
	for (int i = 0; i < 12; i++)
	{
		midi::play_track(good, true);
		auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300 + 70 * i);
		int volume = 0;
		while (std::chrono::steady_clock::now() < until)
		{
			midi::SetVolume(volume++ % (MIX_MAX_VOLUME + 1));
			std::this_thread::sleep_for(std::chrono::microseconds(200));
		}
		restarts++;
	}
	// Let the last play render a few callbacks, at full volume.
	midi::SetVolume(MIX_MAX_VOLUME);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	auto fullGain = fluid_synth_get_gain(Synth);
	midi::music_stop();
	printf("restarts %d, gain changes applied %d, callbacks with an earlier play's voices %d (at most %d voices)\n",
	       restarts, GainCalls - gainCallsBefore, OldVoiceCallbacks.load(), OldVoicesMax.load());
	Expect(MainCallsWhileAttached == 0, "no synthesizer call from the main thread while the hook is attached");
	Expect(GainCalls - gainCallsBefore > 0, "volume changes during playback reach the synthesizer");
	Expect(OldVoiceCallbacks == 0, "no voice of a stopped play remains after the new play's first callback");

	// 4. Volume changes while nothing plays (the hook is detached, nothing renders).
	fluid_voice_t* list[1024];
	auto stoppedVoices = ListedVoices(list, 1024);
	gainCallsBefore = GainCalls;
	const int lastVolume = 37;
	for (int i = 0; i <= 2000; i++)
		midi::SetVolume(i < 2000 ? i % (MIX_MAX_VOLUME + 1) : lastVolume);
	auto gainCallsStopped = GainCalls - gainCallsBefore;
	midi::music_play();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	auto resumedGain = fluid_synth_get_gain(Synth);
	auto expectedGain = fullGain * lastVolume / MIX_MAX_VOLUME;
	midi::music_stop();
	printf("voices listed after the stop %d, synthesizer gain calls for 2001 volume changes while stopped %d, "
	       "gain after the next play %.4f (expected %.4f)\n",
	       stoppedVoices, gainCallsStopped, resumedGain, expectedGain);
	Expect(gainCallsStopped == 0, "volume changes while nothing plays make no synthesizer call");
	Expect(resumedGain > expectedGain - 0.001f && resumedGain < expectedGain + 0.001f,
	       "the next play applies the last volume set while nothing played");

	midi::music_shutdown();
	Mix_CloseAudio();
	SDL_Quit();
	printf("%s\n", Failures ? "FAIL" : "PASS");
	return Failures ? 1 : 0;
}
