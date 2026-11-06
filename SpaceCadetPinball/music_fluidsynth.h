#pragma once

#include <cstdint>
#include <vector>

/// Music through one fluidsynth synthesizer for the whole process, used by midi.cpp when the build
/// has fluidsynth (FT_MUSIC_FLUIDSYNTH) and Usable() holds; otherwise midi.cpp plays through SDL_mixer.
///
/// SDL_mixer's own fluidsynth backend creates a synthesizer, which loads its own copy of the
/// SoundFont's presets, for every loaded Mix_Music, and before SDL_mixer 2.8.1 (or with fluidsynth
/// 1.x) it does not rewind a track that plays again. Here a single synthesizer renders into
/// SDL_mixer's music hook, and every play creates a new player from the track's MIDI bytes, so each
/// play starts at the track's beginning. A stop ends every voice, release included, and resets
/// controllers, programs and effects, so nothing of the stopped track sounds in the next play.
///
/// Threads: the main thread uses the synthesizer only while the music hook is detached; while it is
/// attached, only the hook (SDL's audio thread) does. A volume change is applied by the next audio
/// callback or, while nothing plays, by the next play.
class music_fluidsynth
{
public:
	/// True when the build has fluidsynth, the opened mixer output is 16-bit stereo, and one of the
	/// SoundFonts SDL_mixer would use (Mix_GetSoundFonts: SDL_SOUNDFONTS or its default) exists.
	/// Loads nothing.
	static bool Usable();
	/// Creates the synthesizer (a few MB) and, with loadSoundFonts, loads the SoundFonts (the large
	/// part: preset data plus the sample data). Called from midi::music_init, the point where the
	/// SDL_mixer backend creates its first synthesizer (loading the first track): fluidsynth's
	/// one-time set-up draws from the C library's rand(), which Space Cadet's random numbers also
	/// use, so with either backend the draws come at the same point of the sequence.
	/// A failure is reported once; every later Play then returns false.
	static bool Open(bool loadSoundFonts);
	/// Stops what is playing and plays midi (a Standard MIDI File) from its beginning, looping.
	/// Loads the SoundFonts first if Open did not.
	static bool Play(const std::vector<uint8_t>& midi);
	/// Stops playback and ends every voice. No effect when nothing plays.
	static void Stop();
	/// volume 0..MIX_MAX_VOLUME, scaled as SDL_mixer's fluidsynth backend does. Makes no synthesizer
	/// call: the next audio callback applies it, or the next play when nothing plays.
	static void SetVolume(int volume);
	/// Stops playback and frees the synthesizer and the SoundFonts.
	static void Shutdown();
};
