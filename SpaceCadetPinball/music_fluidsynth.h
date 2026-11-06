#pragma once

/// Music through one fluidsynth synthesizer for the whole process, used by midi.cpp when the build
/// has fluidsynth (FT_MUSIC_FLUIDSYNTH) and Usable() holds; otherwise midi.cpp plays through SDL_mixer.
///
/// SDL_mixer's own fluidsynth backend creates a synthesizer, which loads its own copy of the
/// SoundFont's presets, for every loaded Mix_Music, and with fluidsynth 1.x it does not rewind a
/// track that plays again. Here a single synthesizer renders into SDL_mixer's music hook, and every
/// play creates a new player from the track's MIDI bytes, so each play starts at the track's
/// beginning and a stop leaves no voices behind.
class music_fluidsynth
{
public:
	/// True when the build has fluidsynth, the opened mixer output is 16-bit stereo, and one of the
	/// SoundFonts SDL_mixer would use (Mix_GetSoundFonts: SDL_SOUNDFONTS or its default) exists.
	/// Loads nothing.
	static bool Usable();
	/// Creates the synthesizer (a few MB) and, with loadSoundFonts, loads the SoundFonts (the large
	/// part: preset data plus the sample data). Called from midi::music_init, where SDL_mixer's
	/// backend created its first synthesizer: fluidsynth's one-time set-up draws from the C library's
	/// rand(), which Space Cadet's random numbers also use, so the draws happen at the same point.
	/// A failure is reported once; every later Play then returns false.
	static bool Open(bool loadSoundFonts);
	/// Stops what is playing and plays midi (a Standard MIDI File) from its beginning, looping.
	/// Loads the SoundFonts first if Open did not.
	static bool Play(const std::vector<uint8_t>& midi);
	/// Stops playback and silences every voice. No effect when nothing plays.
	static void Stop();
	/// volume 0..MIX_MAX_VOLUME, scaled as SDL_mixer's fluidsynth backend does.
	static void SetVolume(int volume);
	/// Stops playback and frees the synthesizer and the SoundFonts.
	static void Shutdown();
};
