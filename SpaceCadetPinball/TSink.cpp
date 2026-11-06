#include "pch.h"
#include "TSink.h"


#include "control.h"
#include "loader.h"
#include "render.h"
#include "TPinballTable.h"
#include "TBall.h"
#include "TDrain.h"
#include "pb.h"
#include "timer.h"

TSink::TSink(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	visualStruct visual{};

	MessageField = 0;
	loader::query_visual(groupIndex, 0, &visual);
	BallThrowDirection = visual.Kicker.ThrowBallDirection;
	ThrowAngleMult = visual.Kicker.ThrowBallAngleMult;
	ThrowSpeedMult1 = visual.Kicker.Boost;
	ThrowSpeedMult2 = visual.Kicker.ThrowBallMult * 0.01f;
	SoundIndex4 = visual.SoundIndex4;
	SoundIndex3 = visual.SoundIndex3;
	auto floatArr = loader::query_float_attribute(groupIndex, 0, 601);
	BallPosition.X = floatArr[0];
	BallPosition.Y = floatArr[1];
	EjectZ = floatArr[3]; // eject Z offset (601[3]; 0.0 in every CADET sink)
	// Eject mask = 1 << attr 1301 when present, else the part's collision group
	auto ejectLayer = loader::query_float_attribute_opt(groupIndex, 0, 1301);
	EjectMask = ejectLayer ? 1 << static_cast<int>(floor(ejectLayer[0])) : visual.CollisionGroup;
	TimerTime = *loader::query_float_attribute(groupIndex, 0, 407);
}

int TSink::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TSinkResetTimer:
		if (value < 0.0f)
			value = TimerTime;
		timer::set(value, this, TimerExpired);
		break;
	case MessageCode::PlayerChanged:
		timer::kill(TimerExpired);
		PlayerMessagefieldBackup[PinballTable->CurrentPlayer] = MessageField;
		MessageField = PlayerMessagefieldBackup[static_cast<int>(floor(value))];
		break;
	case MessageCode::Reset:
		{
			timer::kill(TimerExpired);
			MessageField = 0;
			for (auto &msgBackup : PlayerMessagefieldBackup)
				msgBackup = 0;
			break;
		}
	default:
		break;
	}
	return 0;
}

void TSink::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (PinballTable->TiltLockFlag)
	{
		PinballTable->Drain->Collision(ball, nextPosition, direction, distance, edge);
	}
	else
	{
		ball->Disable();
		loader::play_sound(SoundIndex4, ball, "TSink1");
		control::handler(MessageCode::ControlCollision, this);
	}
}

void TSink::TimerExpired(int timerId, void* caller)
{
	auto sink = static_cast<TSink*>(caller);
	auto table = sink->PinballTable;
	if (table->BallCountInRect(sink->BallPosition, table->CollisionCompOffset * 2.0f))
	{
		timer::set(0.5f, sink, TimerExpired);
	}
	else 
	{
		auto ball = table->AddBall(sink->BallPosition);
		assertm(ball, "Failure to create ball in sink");
		ball->CollisionDisabledFlag = true;
		ball->CollisionMask = sink->EjectMask;
		ball->Position.Z = sink->EjectZ + ball->Radius;
		ball->throw_ball(&sink->BallThrowDirection, sink->ThrowAngleMult, sink->ThrowSpeedMult1,
			sink->ThrowSpeedMult2);
		if (sink->SoundIndex3)
			loader::play_sound(sink->SoundIndex3, ball, "TSink2");
		if (pb::IsFullTiltTable())
			control::handler(MessageCode::ControlTimerExpired, sink); // FT 0x418640
	}
}
