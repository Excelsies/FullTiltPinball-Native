#include "pch.h"
#include "TKickback.h"


#include "control.h"
#include "loader.h"
#include "maths.h"
#include "pb.h"
#include "render.h"
#include "timer.h"
#include "TPinballTable.h"

TKickback::TKickback(TPinballTable* table, int groupIndex): TCollisionComponent(table, groupIndex, true)
{
	MessageField = 0;
	Timer = 0;
	KickActiveFlag = 0;
	DisabledFlag = 0;
	TimerTime = 0.7f;
	TimerTime2 = 0.1f;
	// FT ctor 0x415b30: attribute 407 [0],[1] when present, else 0.7 / 0.1 (CADET.DAT has none).
	if (auto times = loader::query_float_attribute_opt(groupIndex, 0, 407))
	{
		TimerTime = times[0];
		TimerTime2 = times[1];
	}
	Threshold = 1000000000.0f;
}

void TKickback::FtReset()
{
	DisabledFlag = 0;
	KickActiveFlag = 0;
	Threshold = 1000000000.0f;
	if (Timer)
		timer::kill(Timer);
	Timer = 0;
	SpriteSet(0);
}

int TKickback::Message(MessageCode code, float value)
{
	// New Full Tilt codes, never sent by Space Cadet: ungated (msgmap 0x4b / 0x4c).
	if (code == MessageCode::FtPartDisable)
	{
		DisabledFlag = 1;
		return 0;
	}
	if (code == MessageCode::FtPartEnable)
	{
		FtReset();
		return 0;
	}
	if (pb::IsFullTiltTable())
	{
		// FT 0x415d70: PlayerChanged swaps the per-player copy only; Reset also zeroes the copies.
		if (code == MessageCode::PlayerChanged)
			SwapPlayerState(value);
		else if (code == MessageCode::SetTiltLock || code == MessageCode::Reset)
		{
			if (code == MessageCode::Reset)
				ResetPlayerState();
			FtReset();
		}
		return 0;
	}
	if ((code == MessageCode::SetTiltLock || code == MessageCode::Reset) && Timer)
	{
		timer::kill(Timer);
		SpriteSet(-1);
		Timer = 0;
		KickActiveFlag = 0;
		Threshold = 1000000000.0;
	}
	return 0;
}

void TKickback::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                          TEdgeSegment* edge)
{
	if (PinballTable->TiltLockFlag)
	{
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 0.0,
		                       Boost);
	}
	else if (pb::IsFullTiltTable())
	{
		// FT Collision 0x415ba0: arm once (not while disabled); a kick plays the sound, restarts the
		// timer as the end-of-kick timer.
		if (!DisabledFlag && !KickActiveFlag)
		{
			KickActiveFlag = 1;
			Timer = timer::set(TimerTime, this, FtTimerArmed);
		}
		if (DefaultCollision(ball, nextPosition, direction))
		{
			loader::play_sound(HardHitSoundId, this, "TKickback");
			KickActiveFlag = 0;
			if (Timer)
				timer::kill(Timer);
			Timer = timer::set(TimerTime2, this, FtTimerEnd);
		}
	}
	else
	{
		if (!KickActiveFlag)
		{
			Threshold = 1000000000.0;
			KickActiveFlag = 1;
			Timer = timer::set(TimerTime, this, TimerExpired);
		}
		if (DefaultCollision(ball, nextPosition, direction))
			KickActiveFlag = 0;
	}
}

void TKickback::TimerExpired(int timerId, void* caller)
{
	auto kick = static_cast<TKickback*>(caller);

	if (kick->KickActiveFlag)
	{
		kick->Threshold = 0.0;
		kick->Timer = timer::set(kick->TimerTime2, kick, TimerExpired);
		loader::play_sound(kick->HardHitSoundId, kick, "TKickback");
		kick->SpriteSet(1);
	}
	else
	{
		kick->SpriteSet(0);
		kick->Timer = 0;
		control::handler(MessageCode::ControlTimerExpired, kick);
	}
}

void TKickback::FtTimerArmed(int timerId, void* caller)
{
	// FT 0x415c60
	auto kick = static_cast<TKickback*>(caller);
	kick->Threshold = 0.0f;
	kick->SpriteSet(1);
	kick->Timer = timer::set(kick->TimerTime2, kick, FtTimerEnd);
}

void TKickback::FtTimerEnd(int timerId, void* caller)
{
	// FT 0x415ce0
	auto kick = static_cast<TKickback*>(caller);
	kick->SpriteSet(0);
	kick->Timer = 0;
	kick->Threshold = 1000000000.0f;
	if (!kick->KickActiveFlag)
		control::handler(MessageCode::ControlTimerExpired, kick);
	kick->KickActiveFlag = 0;
}

#ifdef FT_TEST_HOOK
bool TKickback::HookGet(const std::string& field, double& out) const
{
	if (field == "time1") out = TimerTime;
	else if (field == "time2") out = TimerTime2;
	else if (field == "disabled") out = DisabledFlag;
	else if (field == "kick_active") out = KickActiveFlag;
	else if (field == "threshold") out = Threshold;
	else if (field == "timer_armed") out = Timer != 0 ? 1 : 0;
	else return false;
	return true;
}
#endif
