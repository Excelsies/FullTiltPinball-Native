#include "pch.h"
#include "music_fluidsynth.h"

#ifdef FT_MUSIC_FLUIDSYNTH

#include <fluidsynth.h>

#include <atomic>

namespace
{
	fluid_settings_t* Settings = nullptr;
	fluid_synth_t* Synth = nullptr;
	fluid_player_t* Player = nullptr;
	bool SoundFontsLoaded = false;
	bool Failed = false; // reported once; every later play returns false
	float FullGain = 1.0f; // gain at MIX_MAX_VOLUME
	int Volume = MIX_MAX_VOLUME; // the music volume last set (main thread)
	// Threads: the synthesizer is used by the main thread only while the music hook is detached, and by
	// Render (SDL's audio thread) while it is attached. A volume change waits here until the next audio
	// callback or, while nothing plays, the next play applies it; -1 = none pending.
	std::atomic<int> PendingVolume{-1};

	void ApplyPendingVolume()
	{
		auto volume = PendingVolume.exchange(-1);
		if (volume >= 0)
			fluid_synth_set_gain(Synth, FullGain * volume / MIX_MAX_VOLUME);
	}

	int SDLCALL SoundFontExists(const char* path, void*)
	{
		auto file = fopenu(path, "rb");
		if (!file)
			return 0;
		fclose(file);
		return 1;
	}

	/// Mix_EachSoundFont callback: loads one existing SoundFont into the synthesizer.
	int SDLCALL LoadSoundFont(const char* path, void* synth)
	{
		if (!SoundFontExists(path, nullptr))
			return 0;
		if (fluid_synth_sfload(static_cast<fluid_synth_t*>(synth), path, 1) == FLUID_FAILED)
		{
			printf("midi: fluidsynth could not load SoundFont %s\n", path);
			return 0;
		}
		printf("midi: fluidsynth loaded SoundFont %s\n", path);
		return 1;
	}

	/// Music hook. SDL_mixer clears the buffer before it calls the hook and mixes the sound effects
	/// on top afterwards, as with its own music; Usable() guarantees 16-bit stereo.
	void SDLCALL Render(void*, Uint8* stream, int len)
	{
		ApplyPendingVolume();
		fluid_synth_write_s16(Synth, len / 4, stream, 0, 2, stream, 1, 2);
	}

	void DeleteSynth()
	{
		if (Synth)
			delete_fluid_synth(Synth);
		if (Settings)
			delete_fluid_settings(Settings);
		Synth = nullptr;
		Settings = nullptr;
	}

	bool Fail(const char* why)
	{
		printf("midi: fluidsynth %s; music stays silent\n", why);
		fflush(stdout);
		Failed = true;
		return false;
	}

	bool LoadSoundFonts()
	{
		if (SoundFontsLoaded)
			return true;
		if (Failed || !Synth)
			return false;
		auto start = SDL_GetTicks();
		if (!Mix_EachSoundFont(LoadSoundFont, Synth))
			return Fail("loaded no SoundFont");
		SoundFontsLoaded = true;
		printf("midi: fluidsynth SoundFonts loaded in %u ms\n", SDL_GetTicks() - start);
		fflush(stdout);
		return true;
	}
}

bool music_fluidsynth::Open(bool loadSoundFonts)
{
	int frequency = 0, channels = 0;
	Uint16 format = 0;
	Mix_QuerySpec(&frequency, &format, &channels);
	Settings = new_fluid_settings();
	if (Settings)
	{
		fluid_settings_setnum(Settings, "synth.sample-rate", frequency);
		Synth = new_fluid_synth(Settings);
	}
	if (!Synth)
	{
		DeleteSynth();
		return Fail("synthesizer not created");
	}
	// The gain SDL_mixer's own fluidsynth backend sets for the music volume (FLUIDSYNTH_SetVolume), so
	// music has the level that backend gives it: full volume is gain 1.2 up to SDL_mixer 2.6.x, 1.0 from 2.8.0.
	auto linked = Mix_Linked_Version();
	FullGain = SDL_VERSIONNUM(linked->major, linked->minor, linked->patch) >= SDL_VERSIONNUM(2, 8, 0) ? 1.0f : 1.2f;
	PendingVolume = Volume;
	ApplyPendingVolume();
	printf("midi: fluidsynth synthesizer at %d Hz\n", frequency);
	fflush(stdout);
	return !loadSoundFonts || LoadSoundFonts();
}

bool music_fluidsynth::Usable()
{
	int frequency = 0, channels = 0;
	Uint16 format = 0;
	if (!Mix_QuerySpec(&frequency, &format, &channels))
		return false;
	if (format != AUDIO_S16SYS || channels != 2)
	{
		printf("midi: mixer output is not 16-bit stereo (format 0x%x, %d channels)\n", format, channels);
		return false;
	}
	return Mix_EachSoundFont(SoundFontExists, nullptr) != 0;
}

bool music_fluidsynth::Play(const std::vector<uint8_t>& midi)
{
	Stop();
	if (!LoadSoundFonts())
		return false;

	// The player's timer lives in the synthesizer and runs inside Render: the player is complete
	// before the hook is attached, and Stop detaches the hook before it deletes the player.
	Player = new_fluid_player(Synth);
	if (!Player || fluid_player_add_mem(Player, midi.data(), midi.size()) != FLUID_OK ||
		fluid_player_set_loop(Player, -1) != FLUID_OK || fluid_player_play(Player) != FLUID_OK)
	{
		printf("midi: fluidsynth player not started\n");
		fflush(stdout);
		if (Player)
			delete_fluid_player(Player);
		Player = nullptr;
		return false;
	}
	ApplyPendingVolume();
	Mix_HookMusic(Render, nullptr);
	return true;
}

void music_fluidsynth::Stop()
{
	if (!Player)
		return;
	// Mix_HookMusic takes the mixer's audio lock: Render is not running once it returns.
	Mix_HookMusic(nullptr, nullptr);
	fluid_player_stop(Player);
	delete_fluid_player(Player);
	Player = nullptr;
	// Controllers and programs of the stopped track do not carry into the next play. With fluidsynth 2.x
	// the reset's own "all sounds off" does not end the voices: the reset then sends "all notes off",
	// which puts the voices, still marked as playing, back into their release phase (the queued
	// voice events reach the renderer in that order). The "all sounds off" after it ends them, together
	// with their release, at the start of the next rendered block.
	fluid_synth_system_reset(Synth);
	fluid_synth_all_sounds_off(Synth, -1);
}

void music_fluidsynth::SetVolume(int volume)
{
	// Only stored, also while nothing plays: the voices of a stopped track end only when the next play
	// renders, and fluidsynth queues a gain change for each of them in a bounded queue.
	Volume = volume;
	PendingVolume = volume;
}

void music_fluidsynth::Shutdown()
{
	Stop();
	DeleteSynth();
	SoundFontsLoaded = false;
	Failed = false;
}

#else

bool music_fluidsynth::Usable() { return false; }
bool music_fluidsynth::Open(bool) { return false; }
bool music_fluidsynth::Play(const std::vector<uint8_t>&) { return false; }
void music_fluidsynth::Stop() {}
void music_fluidsynth::SetVolume(int) {}
void music_fluidsynth::Shutdown() {}

#endif
