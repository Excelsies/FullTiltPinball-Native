#include "pch.h"
#include "ft_test_hook.h"
#include "midi.h"


#include "pb.h"


std::vector<Mix_Music*> midi::LoadedTracks{};
Mix_Music* midi::track1, * midi::track2, * midi::track3;
std::vector<Mix_Music*> midi::NamedTracks{};
int midi::ActiveKey = 0, midi::NextKey = 0;
int midi::Volume = MIX_MAX_VOLUME;
bool midi::IsPlaying = false, midi::MixOpen = false;

constexpr uint32_t FOURCC(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	return static_cast<uint32_t>((d << 24) | (c << 16) | (b << 8) | a);
}

int ToVariableLen(uint32_t value, uint32_t& dst)
{
	auto count = 1;
	dst = value & 0x7F;

	while ((value >>= 7))
	{
		dst <<= 8;
		dst |= ((value & 0x7F) | 0x80);
		count++;
	}

	return count;
}

void midi::music_play()
{
	if (!IsPlaying)
	{
		IsPlaying = true;
		auto key = NextKey;
		NextKey = 0;
		ft_test_hook::TrackEvent(key < 0 ? -key : key, true); // as play_track(NextTrack) logged
		play_key(key, true);
	}
}

void midi::music_stop()
{
	if (IsPlaying)
	{
		IsPlaying = false;
		NextKey = ActiveKey;
		StopPlayback();
	}
}

void midi::StopPlayback()
{
	if (ActiveKey != 0)
	{
		if (MixOpen)
			Mix_HaltMusic();
		ActiveKey = 0;
	}
}

int midi::music_init(bool mixOpen, int volume)
{
	MixOpen = mixOpen;
	SetVolume(volume);
	ActiveKey = 0;
	NextKey = 0;
	IsPlaying = false;
	track1 = track2 = track3 = nullptr;
	NamedTracks.clear();

	if (pb::IsFullTiltTable())
	{
		// Dragon's Keep / Skulduggery: the table rules load their own named tracks (load_track) once the
		// table is built; there is nothing to load yet. Music stays available whenever audio is open.
		return MixOpen;
	}

	if (pb::FullTiltMode)
	{
		track1 = load_music("TABA1");
		track2 = load_music("TABA2");
		track3 = load_music("TABA3");

		// FT demo .006 has only one music track, but it is nearly 9 min. long
		if (!track1 && pb::FullTiltDemoMode)
			track1 = load_music("DEMO");
	}
	else
	{
		// 3DPB has only one music track. PINBALL2.MID is a bitmap font, in the same format as PB_MSGFT.bin
		track1 = load_music("PINBALL");
	}

	return track1 != nullptr;
}

void midi::music_shutdown()
{
	music_stop();

	for (auto midi : LoadedTracks)
	{
		Mix_FreeMusic(midi);
	}
	ActiveKey = 0;
	LoadedTracks.clear();
	NamedTracks.clear();
}

void midi::SetVolume(int volume)
{
	Volume = volume;
	if (MixOpen)
		Mix_VolumeMusic(volume);
}

int midi::load_track(const std::string& name)
{
	auto audio = load_music(name);
	if (!audio)
	{
		printf("midi: track %s not loaded (audio closed or no file)\n", name.c_str());
		fflush(stdout);
		return 0;
	}
	NamedTracks.push_back(audio);
	auto handle = static_cast<int>(NamedTracks.size());
	printf("midi: track %s loaded as handle %d\n", name.c_str(), handle);
	fflush(stdout);
	return handle;
}

Mix_Music* midi::load_music(std::string fileName)
{
	if (!MixOpen || pb::quickFlag)
		return nullptr;

	if (pb::FullTiltMode)
	{
		// FT sounds are in SOUND subfolder
		fileName.insert(0, 1, PathSeparator);
		fileName.insert(0, "SOUND");
	}

	// Full Tilt opens only .MDS (its exe has no .MID string): .MDS first there, so the .MID twins that
	// the PIRATE folder ships are not picked. 3DPB music is MIDI only.
	auto audio = load_track_sub(fileName, !pb::FullTiltMode);
	if (!audio)
		audio = load_track_sub(fileName, pb::FullTiltMode);

	if (!audio)
		return nullptr;

	LoadedTracks.push_back(audio);
	return audio;
}

Mix_Music* midi::load_track_sub(std::string fileName, bool isMidi)
{
	// FT has music in two formats, depending on game version: MIDI in 16bit, MIDS in 32bit.
	// 3DPB music is MIDI only.
	Mix_Music* audio = nullptr;
	fileName += isMidi ? ".MID" : ".MDS";
	// Probes: the name as given, all lower case, then (Full Tilt tables only) all upper case, because the Full Tilt
	// data ships upper case names (SOUND/TABC7.MDS) and the rules ask for lower case ones; a case-sensitive
	// file system finds only the upper case spelling. The third probe is reached only when the first two found nothing,
	// so a file that loaded before loads the same way; 3DPB keeps the two upstream probes.
	const int probeCount = pb::FullTiltMode ? 3 : 2;
	for (int i = 0; i < probeCount; i++)
	{
		if (i == 1)
			std::transform(fileName.begin(), fileName.end(), fileName.begin(),
			               [](unsigned char c) { return std::tolower(c); });
		else if (i == 2)
			std::transform(fileName.begin(), fileName.end(), fileName.begin(),
			               [](unsigned char c) { return std::toupper(c); });
		if (isMidi)
		{
			auto filePath = pb::make_path_name(fileName);
			auto fileHandle = fopenu(filePath.c_str(), "rb");
			if (fileHandle)
			{
				fclose(fileHandle);
				auto rw = SDL_RWFromFile(filePath.c_str(), "rb");
				audio = Mix_LoadMUS_RW(rw, 1);
				break;
			}
		}
		else
		{
			auto midi = MdsToMidi(pb::make_path_name(fileName));
			if (midi)
			{
				printf("midi: opened %s as MDS\n", fileName.c_str());
				// Dump converted MIDI file
				/*auto filePath = fileName + ".midi";
				FILE* fileHandle = fopenu(filePath.c_str(), "wb");
				fwrite(midi->data(), 1, midi->size(), fileHandle);
				fclose(fileHandle);*/

				auto rw = SDL_RWFromMem(midi->data(), static_cast<int>(midi->size()));
				audio = Mix_LoadMUS_RW(rw, 1); // This call seems to leak memory no matter what.
				delete midi;
				break;
			}
		}
	}

	return audio;
}

bool midi::play_track(MidiTracks track, bool replay)
{
	ft_test_hook::TrackEvent(static_cast<int>(track), replay);
	return play_key(static_cast<int>(track), replay);
}

bool midi::play_track(int handle, bool replay)
{
	ft_test_hook::TrackEvent(handle, replay);
	assertm(handle >= 0 && handle <= static_cast<int>(NamedTracks.size()), "midi: unknown track handle");
	if (handle == 0)
	{
		// FT play_track stops the current music before it tests the handle for null.
		// When music is paused nothing is playing, so nothing changes.
		StopPlayback();
		return false;
	}
	return play_key(-handle, replay);
}

bool midi::play_key(int key, bool replay)
{
	auto midi = KeyToMusic(key);
	if (!midi || (!replay && ActiveKey == key))
		return false;

	StopPlayback();

	if (!IsPlaying)
	{
		NextKey = key;
		return false;
	}

	if (MixOpen && Mix_PlayMusic(midi, -1))
	{
		ActiveKey = 0;
		return false;
	}

	// On Windows, MIDI volume can only be set during playback.
	// And it changes application master volume for some reason.
	SetVolume(Volume);
	ActiveKey = key;
	return true;
}

MidiTracks midi::get_active_track()
{
	auto key = IsPlaying ? ActiveKey : NextKey;
	return key >= 1 && key <= 3 ? static_cast<MidiTracks>(key) : MidiTracks::None;
}

int midi::active_track()
{
	auto key = IsPlaying ? ActiveKey : NextKey;
	return key < 0 ? -key : 0;
}

Mix_Music* midi::KeyToMusic(int key)
{
	if (key < 0)
	{
		auto index = static_cast<size_t>(-key) - 1;
		return index < NamedTracks.size() ? NamedTracks[index] : nullptr;
	}
	switch (key)
	{
	case 1:
		return track1;
	case 2:
		return track2;
	case 3:
		return track3;
	default:
		return nullptr;
	}
}

/// <summary>
/// SDL_mixed does not support MIDS. To support FT music, a conversion to MIDI is required.
/// </summary>
/// <param name="file">Path to .MDS file</param>
/// <returns>Vector that contains MIDI file</returns>
std::vector<uint8_t>* midi::MdsToMidi(std::string file)
{
	auto fileHandle = fopenu(file.c_str(), "rb");
	if (!fileHandle)
		return nullptr;

	fseek(fileHandle, 0, SEEK_END);
	auto fileSize = static_cast<uint32_t>(ftell(fileHandle));
	auto buffer = new uint8_t[fileSize];
	auto fileBuf = reinterpret_cast<riff_header*>(buffer);
	fseek(fileHandle, 0, SEEK_SET);
	fread(fileBuf, 1, fileSize, fileHandle);
	fclose(fileHandle);

	int returnCode = 0;
	std::vector<uint8_t>* midiOut = nullptr;
	do
	{
		if (fileSize < 12)
		{
			returnCode = 3;
			break;
		}
		if (fileBuf->Riff != FOURCC('R', 'I', 'F', 'F') ||
			fileBuf->Mids != FOURCC('M', 'I', 'D', 'S') ||
			fileBuf->Fmt != FOURCC('f', 'm', 't', ' '))
		{
			returnCode = 3;
			break;
		}
		if (fileBuf->FileSize > fileSize - 8)
		{
			returnCode = 3;
			break;
		}
		if (fileSize - 12 < 8)
		{
			returnCode = 3;
			break;
		}
		if (fileBuf->FmtSize < 12 || fileBuf->FmtSize > fileSize - 12)
		{
			returnCode = 3;
			break;
		}

		auto streamIdUsed = fileBuf->dwFlags == 0;
		auto dataChunk = reinterpret_cast<riff_data*>(reinterpret_cast<char*>(&fileBuf->dwTimeFormat) + fileBuf->
			FmtSize);
		if (dataChunk->Data != FOURCC('d', 'a', 't', 'a'))
		{
			returnCode = 3;
			break;
		}
		if (dataChunk->DataSize < 4)
		{
			returnCode = 3;
			break;
		}

		auto srcPtr = dataChunk->Blocks;
		std::vector<midi_event> midiEvents{};
		for (auto blockIndex = dataChunk->BlocksPerChunk; blockIndex; blockIndex--)
		{
			auto eventSizeInt = streamIdUsed ? 3 : 2;
			auto eventCount = srcPtr->CbBuffer / (4 * eventSizeInt);

			auto currentTicks = srcPtr->TkStart;
			auto srcPtr2 = reinterpret_cast<uint32_t*>(srcPtr->AData);
			for (auto i = 0u; i < eventCount; i++)
			{
				currentTicks += srcPtr2[0];
				auto event = streamIdUsed ? srcPtr2[2] : srcPtr2[1];
				midiEvents.push_back({currentTicks, event});
				srcPtr2 += eventSizeInt;
			}

			srcPtr = reinterpret_cast<riff_block*>(&srcPtr->AData[srcPtr->CbBuffer]);
		}

		// MIDS events can be out of order in the file
		std::sort(midiEvents.begin(), midiEvents.end(), [](const midi_event& lhs, const midi_event& rhs)
		{
			return lhs.iTicks < rhs.iTicks;
		});

		// MThd chunk
		std::vector<uint8_t>& midiBytes = *new std::vector<uint8_t>();
		midiOut = &midiBytes;
		midi_header header(SwapByteOrderShort(static_cast<uint16_t>(fileBuf->dwTimeFormat)));
		auto headerData = reinterpret_cast<const uint8_t*>(&header);
		midiBytes.insert(midiBytes.end(), headerData, headerData + sizeof header);

		// MTrk chunk
		midi_track track(7);
		auto trackData = reinterpret_cast<const uint8_t*>(&track);
		midiBytes.insert(midiBytes.end(), trackData, trackData + sizeof track);
		auto lengthPos = midiBytes.size() - 4;

		auto prevTime = 0u;
		for (const auto& event : midiEvents)
		{
			assertm(event.iTicks >= prevTime, "MIDS events: negative delta-time");
			uint32_t delta = event.iTicks - prevTime;
			prevTime = event.iTicks;

			// Delta time is in variable quantity, Big Endian
			uint32_t deltaVarLen;
			auto count = ToVariableLen(delta, deltaVarLen);
			auto deltaData = reinterpret_cast<const uint8_t*>(&deltaVarLen);
			midiBytes.insert(midiBytes.end(), deltaData, deltaData + count);

			switch (event.iEvent >> 24)
			{
			case 0:
				{
					// Type 0 - MIDI short message. 3 bytes: xx p1 p2 00, where xx - message, p* - parameters
					// Some of the messages have only one parameter
					auto msgMask = (event.iEvent) & 0xF0;
					auto shortMsg = (msgMask == 0xC0 || msgMask == 0xD0);
					auto eventData = reinterpret_cast<const uint8_t*>(&event.iEvent);
					midiBytes.insert(midiBytes.end(), eventData, eventData + (shortMsg ? 2 : 3));
					break;
				}
			case 1:
				{
					// Type 1 - tempo change, 3 bytes: xx xx xx 01
					// Meta message, set tempo, 3 bytes payload
					const uint8_t metaSetTempo[] = {0xFF, 0x51, 0x03};
					midiBytes.insert(midiBytes.end(), metaSetTempo, metaSetTempo + 3);

					auto eventBE = SwapByteOrderInt(event.iEvent);
					auto eventData = reinterpret_cast<const uint8_t*>(&eventBE) + 1;
					midiBytes.insert(midiBytes.end(), eventData, eventData + 3);
					break;
				}
			default:
				assertm(0, "MIDS events: uknown event");
				break;
			}
		}

		// Meta message, end of track, 0 bytes payload
		const uint8_t metaEndTrack[] = {0x00, 0xFF, 0x2f, 0x00};
		midiBytes.insert(midiBytes.end(), metaEndTrack, metaEndTrack + 4);

		// Set final MTrk size
		auto lengthBE = SwapByteOrderInt(static_cast<uint32_t>(midiBytes.size()) - sizeof header - sizeof track);
		auto lengthData = reinterpret_cast<const uint8_t*>(&lengthBE);
		std::copy_n(lengthData, 4, midiBytes.begin() + lengthPos);
	}
	while (false);

	delete[] buffer;
	if (returnCode && midiOut)
	{
		delete midiOut;
		midiOut = nullptr;
	}

	return midiOut;
}
