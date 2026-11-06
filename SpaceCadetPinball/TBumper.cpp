#include "pch.h"
#include "TBumper.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "timer.h"
#include "TPinballTable.h"

TBumper::TBumper(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	visualStruct visual{};

	BmpIndex = 0;
	Timer = 0;
	TimerTime = *loader::query_float_attribute(groupIndex, 0, 407);
	loader::query_visual(groupIndex, 0, &visual);
	SoundIndex4 = visual.SoundIndex4;
	SoundIndex3 = visual.SoundIndex3;
	OriginalThreshold = Threshold;
}

int TBumper::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TBumperSetBmpIndex:
		{
			auto nextBmp = static_cast<int>(floor(value));
			auto maxBmp = static_cast<int>(ListBitmap->size()) - 1;
			if (2 * nextBmp > maxBmp)
				nextBmp = maxBmp / 2;
			if (nextBmp < 0)
				nextBmp = 0;
			if (nextBmp != BmpIndex)
			{
				if (nextBmp >= BmpIndex)
					loader::play_sound(SoundIndex4, this, "TBumper1");
				if (nextBmp < BmpIndex)
					loader::play_sound(SoundIndex3, this, "TBumper2");
				BmpIndex = nextBmp;
				Fire();
				control::handler(MessageCode::TBumperSetBmpIndex, this);
			}
			break;
		}
	case MessageCode::TBumperIncBmpIndex:
		{
			auto nextBmp = BmpIndex + 1;
			auto maxBmp = static_cast<int>(ListBitmap->size()) - 1;
			if (2 * nextBmp > maxBmp)
				nextBmp = maxBmp / 2;
			TBumper::Message(MessageCode::TBumperSetBmpIndex, static_cast<float>(nextBmp));
			break;
		}
	case MessageCode::TBumperDecBmpIndex:
		{
			auto nextBmp = BmpIndex - 1;
			if (nextBmp < 0)
				nextBmp = 0;
			TBumper::Message(MessageCode::TBumperSetBmpIndex, static_cast<float>(nextBmp));
			break;
		}
	case MessageCode::PlayerChanged:
		{
			if (pb::IsFullTiltTable())
			{
				// FT 0x414160, code 0x3fe: base per-player swap, a running fire timer is killed and its
				// callback run, then the per-player BmpIndex swap.
				SwapPlayerState(value);
				if (Timer)
				{
					timer::kill(Timer);
					TimerExpired(Timer, this);
				}
				PlayerData[PinballTable->CurrentPlayer].BmpIndex = BmpIndex;
				BmpIndex = PlayerData[static_cast<int>(floor(value))].BmpIndex;
				TBumper::Message(MessageCode::TBumperSetBmpIndex, static_cast<float>(BmpIndex));
				break;
			}

			auto playerPtr = &PlayerData[PinballTable->CurrentPlayer];
			playerPtr->BmpIndex = BmpIndex;
			playerPtr->MessageField = MessageField;

			playerPtr = &PlayerData[static_cast<int>(floor(value))];
			BmpIndex = playerPtr->BmpIndex;
			MessageField = playerPtr->MessageField;
			TBumper::Message(MessageCode::TBumperSetBmpIndex, static_cast<float>(BmpIndex));
			break;
		}
	case MessageCode::Reset:
		{
			if (Timer)
			{
				timer::kill(Timer);
				TimerExpired(Timer, this);
			}
			if (pb::IsFullTiltTable())
			{
				// FT 0x414160, code 0x402: base reset, then only PlayerCount saved BmpIndex entries cleared.
				ResetPlayerState();
				BmpIndex = 0;
				for (auto i = 0; i < PinballTable->PlayerCount; i++)
					PlayerData[i].BmpIndex = 0;
				TimerExpired(0, this);
				break;
			}
			BmpIndex = 0;
			MessageField = 0;
			for (auto& playerPtr : PlayerData)
			{
				playerPtr.BmpIndex = 0;
				playerPtr.MessageField = 0;
			}
			TimerExpired(0, this);
			break;
		}
	default:
		break;
	}

	return 0;
}

void TBumper::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (DefaultCollision(ball, nextPosition, direction))
	{
		Fire();
		control::handler(MessageCode::ControlCollision, this);
	}
}

void TBumper::TimerExpired(int timerId, void* caller)
{
	auto bump = static_cast<TBumper*>(caller);
	bump->SpriteSet(bump->BmpIndex * 2);
	bump->Timer = 0;
	bump->Threshold = bump->OriginalThreshold;
}

void TBumper::Fire()
{
	SpriteSet(2 * BmpIndex + 1);
	Timer = timer::set(TimerTime, this, TimerExpired);
	Threshold = 1000000000.0;
}

#ifdef FT_TEST_HOOK
bool TBumper::HookGet(const std::string& field, double& out) const
{
	if (field == "bmp_index") { out = BmpIndex; return true; }
	if (field == "timer") { out = Timer != 0 ? 1 : 0; return true; }
	if (field.compare(0, 9, "saved_bmp") == 0 && field.size() == 10 && field[9] >= '0' && field[9] <= '3')
	{
		out = PlayerData[field[9] - '0'].BmpIndex;
		return true;
	}
	return false;
}

bool TBumper::HookSet(const std::string& field, double value)
{
	if (field.compare(0, 9, "saved_bmp") == 0 && field.size() == 10 && field[9] >= '0' && field[9] <= '3')
	{
		PlayerData[field[9] - '0'].BmpIndex = static_cast<int>(value);
		return true;
	}
	return false;
}
#endif
