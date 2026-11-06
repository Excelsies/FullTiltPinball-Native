#include "pch.h"
#include "TSoloTarget.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "timer.h"
#include "TPinballTable.h"

TSoloTarget::TSoloTarget(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	visualStruct visual{};

	Timer = 0;
	TimerTime = 0.1f;
	loader::query_visual(groupIndex, 0, &visual);
	SoundIndex4 = visual.SoundIndex4;
	TSoloTarget::Message(MessageCode::TSoloTargetEnable, 0.0);
}

int TSoloTarget::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TSoloTargetDisable:
	case MessageCode::TSoloTargetEnable:
		// FT 0x414be0: both codes kill a pending re-enable timer first (upstream only on Reset); for every table
		if (Timer)
			timer::kill(Timer);
		Timer = 0;
		ActiveFlag = code == MessageCode::TSoloTargetEnable;
		break;
	case MessageCode::PlayerChanged:
		// FT 0x414be0 case 0x3fe: per-player MessageField swap; upstream ignores it
		if (pb::IsFullTiltTable())
			SwapPlayerState(value);
		return 0;
	case MessageCode::Reset:
		if (pb::IsFullTiltTable())
			ResetPlayerState(); // FT case 0x402 calls the base reset first
		if (Timer)
			timer::kill(Timer);
		Timer = 0;
		ActiveFlag = 1;
		break;
	default:
		return 0;
	}

	SpriteSet(1 - ActiveFlag);
	return 0;
}

void TSoloTarget::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                            TEdgeSegment* edge)
{
	if (DefaultCollision(ball, nextPosition, direction))
	{
		Message(MessageCode::TSoloTargetDisable, 0.0);
		Timer = timer::set(TimerTime, this, TimerExpired);
		control::handler(MessageCode::ControlCollision, this);
	}
}

void TSoloTarget::TimerExpired(int timerId, void* caller)
{
	auto target = static_cast<TSoloTarget*>(caller);
	target->Timer = 0; // before Enable, as FT 0x414bc0 (Enable's own kill must not hit the expired id)
	target->Message(MessageCode::TSoloTargetEnable, 0.0);
}
