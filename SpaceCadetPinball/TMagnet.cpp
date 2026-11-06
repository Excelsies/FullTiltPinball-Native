#include "pch.h"
#include "TMagnet.h"

#include "control.h"
#include "TBall.h"
#include "timer.h"

// FT 0x416560: TKickout(table, group, 1), then HoldOffTime = 1.0 f and HoldOff = 0. ActiveFlag stays 1 until the
// start-up Reset broadcast disables the part.
TMagnet::TMagnet(TPinballTable* table, int groupIndex) : TKickout(table, groupIndex, true)
{
	TimerTime2 = 1.0f;
	BallCaputeredFlag = 0;
}

// FT 0x4166c0. Anything not listed is ignored: TKickout::Message is not called.
int TMagnet::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::FtPartEnable: // FT 0x4b
		ActiveFlag = 1;
		break;
	case MessageCode::FtPartDisable: // FT 0x4c
	case MessageCode::SetTiltLock:
		Disable();
		break;
	case MessageCode::PlayerChanged: // FT 0x3fe: per-player save/restore (base 0x413570), then disable
		SwapPlayerState(value);
		Disable();
		break;
	case MessageCode::Reset: // FT 0x402: base 0x413550, then disable
		ResetPlayerState();
		Disable();
		break;
	default:
		break;
	}
	return 0;
}

// Shared tail of FT 0x4166c0: kill the hold-off timer, switch the trigger and the field off, clear HoldOff.
// The original leaves the timer id stale; ids are never reused, so zeroing it changes nothing observable.
void TMagnet::Disable()
{
	if (Timer)
		timer::kill(Timer);
	Timer = 0;
	ActiveFlag = 0;
	BallCaputeredFlag = 0;
}

// FT 0x4165a0: the ball passes through (TRollover-style), the field goes off and a hold-off timer starts. No tilt
// check, no sound, no check of HoldOff; a second crossing during the hold-off starts a second timer (as the original).
void TMagnet::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;
	ball->not_again(edge);
	BallCaputeredFlag = 1;
	Timer = timer::set(TimerTime2, this, TimerExpired);
	control::handler(MessageCode::ControlCollision, this);
}

// FT 0x4166a0
void TMagnet::TimerExpired(int timerId, void* caller)
{
	auto magnet = static_cast<TMagnet*>(caller);
	magnet->BallCaputeredFlag = 0;
	magnet->Timer = 0;
	control::handler(MessageCode::ControlTimerExpired, magnet);
}

#ifdef FT_TEST_HOOK
bool TMagnet::HookGet(const std::string& field, double& out) const
{
	if (field == "hold_off")
	{
		out = BallCaputeredFlag;
		return true;
	}
	if (field == "field_mult")
	{
		out = FieldMult;
		return true;
	}
	if (field == "hold_off_time")
	{
		out = TimerTime2;
		return true;
	}
	return TKickout::HookGet(field, out);
}
#endif
