#include "pch.h"
#include "TPlunger.h"


#include "control.h"
#include "loader.h"
#include "maths.h"
#include "pb.h"
#include "render.h"
#include "TBall.h"
#include "timer.h"
#include "TPinballTable.h"

TPlunger::TPlunger(TPinballTable* table, int groupIndex, bool isLauncher) : TCollisionComponent(table, groupIndex, true)
{
	IsLauncher = isLauncher;
	visualStruct visual{};

	loader::query_visual(groupIndex, 0, &visual);
	Boost = 0.0;
	BallFeedTimer_ = 0;
	PullbackTimer_ = 0;
	SoundIndexP1 = visual.SoundIndex4;
	SoundIndexP2 = visual.SoundIndex3;
	HardHitSoundId = visual.Kicker.HardHitSoundId;
	Threshold = 1000000000.0;

	// FT: MaxPullback = DAT attribute 402, default 50 (FT 0x418800). 3DPB: 100, increment floored.
	// No bitmaps (Skulduggery's launcher): increment = MaxPullback instead of a ListBitmap null deref.
	const bool hasBitmaps = ListBitmap && !ListBitmap->empty();
	if (pb::FullTiltMode)
	{
		auto maxPullbackAttr = loader::query_float_attribute_opt(groupIndex, 0, 402);
		MaxPullback = maxPullbackAttr ? *maxPullbackAttr : 50.0f;
		PullbackIncrement = hasBitmaps ? MaxPullback / (ListBitmap->size() * 8.0f) : MaxPullback;
		if (IsLauncher)
			PullbackIncrement = 0.0f; // FT 0x418800 skips the increment for the launcher flag
	}
	else
	{
		MaxPullback = 100;
		PullbackIncrement = hasBitmaps ? std::floor(MaxPullback / (ListBitmap->size() * 8.0f)) : MaxPullback;
	}

	Elasticity = 0.5f;
	Smoothness = 0.5f;
	
	PullbackDelay = 0.025f;
	float* floatArr = loader::query_float_attribute(groupIndex, 0, 601);
	table->PlungerPosition = {floatArr[0], floatArr[1]};
}

void TPlunger::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (PinballTable->TiltLockFlag || SomeCounter > 0) 
	{
		auto boost = RandFloat() * MaxPullback * 0.1f + MaxPullback;
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 0, boost);
		if (SomeCounter)
			SomeCounter--;
		Message(MessageCode::PlungerInputReleased, 0.0);
	}
	else 
	{
		auto boost = RandFloat() * Boost * 0.1f + Boost;
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, Threshold, boost);
	}
}

int TPlunger::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::PlungerInputPressed:
		if (!PullbackStartedFlag && (!pb::FullTiltMode || PinballTable->MultiballCount > 0 && !PinballTable->
			TiltLockFlag))
		{
			if (IsLauncher)
			{
				// FT 0x418950 case 0x3ec with the launcher flag: fire at once
				Message(MessageCode::PlungerLaunchBall, 0.0f);
				break;
			}
			PullbackStartedFlag = true;
			Boost = 0.0;
			Threshold = 1000000000.0;
			loader::play_sound(HardHitSoundId, this, "TPlunger1");
			PullbackTimer(0, this);
		}
		break;
	case MessageCode::PlungerFeedBall:
		{
			if (PinballTable->BallCountInRect(PinballTable->PlungerPosition, PinballTable->CollisionCompOffset * 1.2f))
			{
				timer::set(1.0f, this, BallFeedTimer);
			}
			else
			{
				auto ball = PinballTable->AddBall(PinballTable->PlungerPosition);
				assertm(ball, "Failure to create ball in plunger");
				PinballTable->MultiballCount++;
				PinballTable->BallInDrainFlag = 0;
				pb::tilt_no_more();
			}
			break;
		}
	case MessageCode::PlungerStartFeedTimer:
		timer::set(0.95999998f, this, BallFeedTimer);
		loader::play_sound(SoundIndexP1, this, "TPlunger2");
		break;
	case MessageCode::PlungerLaunchBall:
		PullbackStartedFlag = true;
		Boost = MaxPullback;
		Message(MessageCode::PlungerInputReleased, 0.0f);
		break;
	case MessageCode::PlungerRelaunchBall:
		SomeCounter++;
		timer::set(value, this, BallFeedTimer);
		loader::play_sound(SoundIndexP1, this, "TPlunger2_1");
		PullbackStartedFlag = true;
		// FT arms the pull-back chain here too; with the launcher flag its increment is 0 and it would re-arm
		// forever without changing Boost, so it is not armed
		if (!IsLauncher)
			PullbackTimer(0, this);
		break;
	case MessageCode::PlayerChanged:
		if (pb::IsFullTiltTable())
			SwapPlayerState(value); // FT 0x418950 case 0x3fe: swap first, then the resets
		PullbackStartedFlag = false;
		Boost = 0.0f;
		Threshold = 1000000000.0f;
		SomeCounter = 0;
		timer::kill(BallFeedTimer);
		timer::kill(PullbackTimer);
		timer::kill(ReleasedTimer);
		break;
	case MessageCode::SetTiltLock:
		SomeCounter = 0;
		timer::kill(BallFeedTimer);
		break;
	case MessageCode::PlungerInputReleased:
	case MessageCode::Resume:
	case MessageCode::LooseFocus:
		if (PullbackStartedFlag && !SomeCounter)
		{
			PullbackStartedFlag = false;
			Threshold = 0.0;
			if (PullbackTimer_)
				timer::kill(PullbackTimer_);
			PullbackTimer_ = 0;
			loader::play_sound(SoundIndexP2, this, "TPlunger3");
			if (!IsLauncher)
				SpriteSet(0);
			// FT 0x418950: 0.2 s (0x3e4ccccd); also for Space Cadet on the Full Tilt data (CADET.EXE keeps the boost on
			// for 0.2 s, which decides the early-press launch); 3D Pinball data keeps upstream's PullbackDelay
			timer::set(pb::FullTiltMode ? 0.2f : PullbackDelay, this, ReleasedTimer);
		}
		break;
	case MessageCode::Reset:
	{
		if (pb::IsFullTiltTable())
			ResetPlayerState(); // FT case 0x402: base reset before the resets
		PullbackStartedFlag = false;
		Boost = 0.0f;
		Threshold = 1000000000.0f;
		SomeCounter = 0;

		timer::kill(BallFeedTimer);
		timer::kill(PullbackTimer);
		timer::kill(ReleasedTimer);

		if (!IsLauncher)
			SpriteSet(0);
		break;
	}
	default:
		break;
	}

	control::handler(code, this);
	return 0;
}

#ifdef FT_TEST_HOOK
bool TPlunger::HookGet(const std::string& field, double& out) const
{
	if (field == "boost")
	{
		out = Boost;
		return true;
	}
	return TCollisionComponent::HookGet(field, out);
}
#endif

void TPlunger::BallFeedTimer(int timerId, void* caller)
{
	auto plunger = static_cast<TPlunger*>(caller);
	plunger->Message(MessageCode::PlungerFeedBall, 0.0);
}

void TPlunger::PullbackTimer(int timerId, void* caller)
{
	auto plunger = static_cast<TPlunger*>(caller);
	plunger->Boost += plunger->PullbackIncrement;
	if (plunger->Boost <= plunger->MaxPullback)
	{
		if (plunger->SomeCounter)
		{
			plunger->PullbackTimer_ = timer::set(plunger->PullbackDelay / 4.0f, plunger, PullbackTimer);
		}
		else
		{
			plunger->PullbackTimer_ = timer::set(plunger->PullbackDelay, plunger, PullbackTimer);
		}
	}
	else
	{
		plunger->PullbackTimer_ = 0;
		plunger->Boost = plunger->MaxPullback;
	}

	// FT guards the sprite update with ListBitmap != 0
	if (plunger->ListBitmap)
	{
		int index = static_cast<int>(floor(
			static_cast<float>(plunger->ListBitmap->size() - 1) *
			(plunger->Boost / plunger->MaxPullback)));
		plunger->SpriteSet(index);
	}
}

void TPlunger::ReleasedTimer(int timerId, void* caller)
{
	auto plunger = static_cast<TPlunger*>(caller);
	plunger->Threshold = 1000000000.0;
	plunger->Boost = 0.0;
}
