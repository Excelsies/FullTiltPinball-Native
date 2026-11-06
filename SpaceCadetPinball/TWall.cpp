#include "pch.h"
#include "TWall.h"


#include "control.h"
#include "pb.h"
#include "render.h"
#include "timer.h"

TWall::TWall(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	if (RenderSprite)
		SpriteSet(-1);
}

int TWall::Message(MessageCode code, float value)
{
	if (pb::IsFullTiltTable())
	{
		// FT 0x413c40: Reset (0x402) and PlayerChanged (0x3fe) both kill the 0.1 s flash timer and run its
		// callback, then the base per-player handling; every other code does nothing.
		if (code == MessageCode::Reset || code == MessageCode::PlayerChanged)
		{
			if (Timer)
			{
				timer::kill(Timer);
				TimerExpired(Timer, this);
			}
			if (code == MessageCode::Reset)
				ResetPlayerState();
			else
				SwapPlayerState(value);
		}
		return 0;
	}

	if (code == MessageCode::Reset && Timer)
	{
		timer::kill(Timer);
		TimerExpired(Timer, this);
	}
	return 0;
}

void TWall::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (DefaultCollision(ball, nextPosition, direction))
	{
		if (ListBitmap)
		{
			SpriteSet(0);
			Timer = timer::set(0.1f, this, TimerExpired);
		}
		control::handler(MessageCode::ControlCollision, this);
	}
}

void TWall::TimerExpired(int timerId, void* caller)
{
	auto wall = static_cast<TWall*>(caller);
	wall->SpriteSet(-1);
	wall->Timer = 0;
	wall->MessageField = 0;
}
