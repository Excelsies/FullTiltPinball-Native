#include "pch.h"
#include "TPopupTarget.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "timer.h"
#include "TPinballTable.h"

TPopupTarget::TPopupTarget(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	Timer = 0;
	FtMode = pb::IsFullTiltTable();
	TimerTime = *loader::query_float_attribute(groupIndex, 0, 407);
}

int TPopupTarget::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TPopupTargetDisable:
		ActiveFlag = 0;
		if (FtMode)
		{
			// FT 0x414a00 case 0x35: a pending raise is cancelled.
			if (Timer)
				timer::kill(Timer);
			Timer = 0;
		}
		SpriteSet(-1);
		break;
	case MessageCode::TPopupTargetEnable:
		Timer = timer::set(TimerTime, this, TimerExpired);
		break;
	case MessageCode::PlayerChanged:
		if (FtMode)
		{
			// FT 0x414a00 case 0x3fe: base per-player MessageField swap (0x413570), cancel a pending raise,
			// save/restore the ActiveFlag itself per player, then re-issue Enable or Disable from the restored flag.
			SwapPlayerState(value);
			if (Timer)
				timer::kill(Timer);
			Timer = 0;
			PlayerActiveBackup[PinballTable->CurrentPlayer] = ActiveFlag;
			ActiveFlag = PlayerActiveBackup[static_cast<int>(floor(value))];
			Message(ActiveFlag ? MessageCode::TPopupTargetEnable : MessageCode::TPopupTargetDisable, 0.0);
			break;
		}
		PlayerMessagefieldBackup[PinballTable->CurrentPlayer] = MessageField;
		MessageField = PlayerMessagefieldBackup[static_cast<int>(floor(value))];
		TPopupTarget::Message(MessageField ? MessageCode::TPopupTargetDisable : MessageCode::TPopupTargetEnable, 0.0);
		break;
	case MessageCode::Reset:
	{
			if (FtMode)
			{
				// FT 0x414a00 case 0x402: base reset (0x413550), cancel a pending raise, raise (retry rule applies),
				// all four saved ActiveFlags = 1.
				ResetPlayerState();
				if (Timer)
					timer::kill(Timer);
				Timer = 0;
				TimerExpired(0, this);
				std::fill(std::begin(PlayerActiveBackup), std::end(PlayerActiveBackup), static_cast<char>(1));
				break;
			}
			MessageField = 0;
			int* playerPtr = PlayerMessagefieldBackup;
			for (auto index = 0; index < PinballTable->PlayerCount; ++index)
			{
				*playerPtr = 0;
				++playerPtr;
			}

			if (Timer)
				timer::kill(Timer);
			TimerExpired(0, this);
			break;
		}
	default:
		break;
	}
	return 0;
}

void TPopupTarget::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                             TEdgeSegment* edge)
{
	if (PinballTable->TiltLockFlag)
	{
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 1000000000.0, 0.0);
	}
	else if (maths::basic_collision(
		ball,
		nextPosition,
		direction,
		Elasticity,
		Smoothness,
		Threshold,
		Boost) > Threshold)
	{
		if (HardHitSoundId)
			loader::play_sound(HardHitSoundId, this, "TPopupTarget1");
		Message(MessageCode::TPopupTargetDisable, 0.0);
		control::handler(MessageCode::ControlCollision, this);
	}
}

void TPopupTarget::TimerExpired(int timerId, void* caller)
{
	auto target = static_cast<TPopupTarget*>(caller);
	target->Timer = 0;
	if (target->FtMode && target->PinballTable->BallCountInRect(target->AABB) != 0)
	{
		// FT 0x414990: a ball inside the AABB postpones the raise by another TimerTime.
		target->Message(MessageCode::TPopupTargetEnable, 0.0);
		return;
	}
	target->ActiveFlag = 1;
	target->SpriteSet(0);
	if (timerId)
	{
		if (target->SoftHitSoundId)
			loader::play_sound(target->SoftHitSoundId, target, "TPopupTarget2");
	}
}

#ifdef FT_TEST_HOOK
bool TPopupTarget::HookGet(const std::string& field, double& out) const
{
	// saved_active<n>: the Full Tilt per-player ActiveFlag copy of player n (FT +0x7e + n).
	if (FtMode && field.size() == 13 && field.compare(0, 12, "saved_active") == 0 && field[12] >= '0' && field[12] <= '3')
	{
		out = PlayerActiveBackup[field[12] - '0'];
		return true;
	}
	return TCollisionComponent::HookGet(field, out);
}
#endif
