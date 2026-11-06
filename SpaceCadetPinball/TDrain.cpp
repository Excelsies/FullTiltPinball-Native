#include "pch.h"
#include "TDrain.h"


#include "control.h"
#include "loader.h"
#include "TBall.h"
#include "timer.h"
#include "pb.h"
#include "TPinballTable.h"

TDrain::TDrain(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	Timer = 0;
	TimerTime = *loader::query_float_attribute(groupIndex, 0, 407);
}

int TDrain::Message(MessageCode code, float value)
{
	// FT 0x418770 case 0x3f3: SetTiltLock kills a pending drain timer. FT tables only: upstream nudges at
	// MultiballCount 0, where a kill could drop the drain's ControlTimerExpired
	if (code == MessageCode::SetTiltLock && pb::IsFullTiltTable())
	{
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		return 0;
	}
	if (code == MessageCode::PlayerChanged && pb::IsFullTiltTable())
	{
		// FT 0x418770 case 0x3fe: per-player MessageField swap
		SwapPlayerState(value);
		return 0;
	}
	if (code == MessageCode::Reset)
	{
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		if (pb::IsFullTiltTable())
			ResetPlayerState(); // FT case 0x402: after the timer kill, before BallInDrainFlag = 0
		PinballTable->BallInDrainFlag = 0;
	}
	return 0;
}

void TDrain::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	ball->Disable();
	--PinballTable->MultiballCount;
	if (PinballTable->MultiballCount <= 0)
	{
		PinballTable->MultiballCount = 0;
		PinballTable->BallInDrainFlag = 1;
		Timer = timer::set(TimerTime, this, TimerCallback);
	}
	control::handler(MessageCode::ControlCollision, this);
}

void TDrain::TimerCallback(int timerId, void* caller)
{
	auto drain = static_cast<TDrain*>(caller);
	control::handler(MessageCode::ControlTimerExpired, drain);
}
