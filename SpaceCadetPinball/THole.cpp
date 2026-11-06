#include "pch.h"
#include "THole.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "TBall.h"
#include "timer.h"
#include "TPinballTable.h"
#include "TTableLayer.h"
#include <string>

THole::THole(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};
	circle_type circle{};

	Unknown4 = 0.050000001f;
	MessageField = 0;
	Timer = 0;
	BallCapturedFlag = false;
	RiseSpeed = 0.0f;
	HeldBall = nullptr;
	const bool fullTilt = pb::IsFullTiltTable();
	Unknown3 = loader::query_float_attribute(groupIndex, 0, 407, 0.25f);
	if (fullTilt)
	{
		// FT 0x416740: fall multiplier from 703 (default 0.5), rise speed from 1306 (default 0)
		GravityMult = loader::query_float_attribute(groupIndex, 0, 703, 0.5f);
		auto riseSpeed = loader::query_float_attribute_opt(groupIndex, 0, 1306);
		RiseSpeed = riseSpeed ? *riseSpeed : 0.0f;
	}
	else
		GravityMult = loader::query_float_attribute(groupIndex, 0, 701, 0.2f);
	GravityPull = *loader::query_float_attribute(groupIndex, 0, 305);

	loader::query_visual(groupIndex, 0, &visual);
	Circle.Center.X = visual.FloatArr[0];
	Circle.Center.Y = visual.FloatArr[1];
	Circle.RadiusSq = *loader::query_float_attribute(groupIndex, 0, 306) * visual.FloatArr[2];
	if (Circle.RadiusSq == 0.0f)
		Circle.RadiusSq = 0.001f;

	auto tCircle = new TCircle(this, &ActiveFlag, visual.CollisionGroup,
	                           reinterpret_cast<vector3*>(visual.FloatArr),
	                           Circle.RadiusSq);
	tCircle->place_in_grid(&AABB);
	EdgeList.push_back(tCircle);

	// Exit Z: FT = ball radius (table CollisionCompOffset, set by the first ball) + 408[3]; upstream 408[2]
	if (fullTilt)
		ZSetValue = table->CollisionCompOffset + loader::query_float_attribute(groupIndex, 0, 408)[3];
	else
		ZSetValue = loader::query_float_attribute(groupIndex, 0, 408)[2];
	CollisionMask = static_cast<int>(floor(*loader::query_float_attribute(groupIndex, 0, 1304)));

	// CollisionMask difference: 3DPB - value given as is, FT: mask = 1 << value
	if (pb::FullTiltMode || fullTilt)
		CollisionMask = 1 << CollisionMask;

	Circle.RadiusSq = visual.FloatArr[2] * visual.FloatArr[2];
	circle.RadiusSq = Circle.RadiusSq;
	circle.Center.X = Circle.Center.X;
	circle.Center.Y = Circle.Center.Y;

	Field.ActiveFlag = &ActiveFlag;
	Field.CollisionComp = this;
	Field.CollisionGroup = visual.CollisionGroup;
	TTableLayer::edges_insert_circle(&circle, nullptr, &Field);
}

int THole::Message(MessageCode code, float value)
{
	if (pb::IsFullTiltTable())
	{
		// Full Tilt Message 0x416c70
		switch (code)
		{
		case MessageCode::SetTiltLock:
			value = -1.0f; // treated as THoleEjectBall(-1)
			// fall through
		case MessageCode::THoleEjectBall:
			if (value <= 0.0f)
			{
				SecondStageNow();
				break;
			}
			if (Timer)
			{
				timer::kill(Timer);
				Timer = 0;
			}
			Timer = timer::set(value, this, TimerExpired);
			break;
		case MessageCode::PlungerFeedBall: // FT 0x3f8: release, THole-internal
			return ReleaseHeldBall();
		case MessageCode::PlayerChanged:
		case MessageCode::Reset:
			if (code == MessageCode::Reset)
				ResetPlayerState();
			else
				SwapPlayerState(value);
			ReleaseRetry.Kill();
			if (BallCapturedFlag)
			{
				if (Timer)
					timer::kill(Timer);
				SecondStageNow();
			}
			break;
		default:
			break;
		}
		return 0;
	}

	if (code == MessageCode::Reset && BallCapturedFlag)
	{
		if (Timer)
			timer::kill(Timer);
		Timer = 0;
		BallCapturedSecondStage = true;
	}
	return 0;
}

void THole::SecondStageNow()
{
	BallCapturedSecondStage = true;
	Timer = 0;
}

int THole::ReleaseHeldBall()
{
	if (!BallCapturedFlag)
		return 0;

	ReleaseRetry.Kill();
	// Wait while another ball with the exit mask is within 2 ball radii of the held ball (0x4130c0).
	const float radius = PinballTable->CollisionCompOffset;
	RectF rect{};
	rect.XMax = radius * 2.0f + HeldBall->Position.X;
	rect.XMin = radius * -2.0f + HeldBall->Position.X;
	rect.YMax = radius * 2.0f + HeldBall->Position.Y;
	rect.YMin = radius * -2.0f + HeldBall->Position.Y;
	if (PinballTable->BallCountInRect(rect, static_cast<unsigned int>(CollisionMask)))
	{
		ReleaseRetry.Set(this, MessageCode::PlungerFeedBall, 0.0f);
		return 0;
	}

	auto ball = HeldBall;
	BallCapturedFlag = false;
	BallCapturedSecondStage = false;
	ball->Position.Z = ZSetValue;
	ball->CollisionDisabledFlag = true;
	ball->CollisionMask = CollisionMask;
	ball->CollisionComp = nullptr;
	HeldBall = nullptr;
	loader::play_sound(SoftHitSoundId, ball, "THole2");
	control::handler(MessageCode::ControlBallReleased, this);
	return 0;
}

#ifdef FT_TEST_HOOK
bool THole::HookGet(const std::string& field, double& out) const
{
	if (field == "captured") out = BallCapturedFlag;
	else if (field == "second_stage") out = BallCapturedSecondStage;
	else if (field == "hold_timer") out = Timer != 0;
	else if (field == "hold_time") out = Unknown3;
	else if (field == "rise_speed") out = RiseSpeed;
	else if (field == "exit_z") out = ZSetValue;
	else if (field == "exit_mask") out = CollisionMask;
	else if (field == "grav_mult") out = GravityMult;
	else if (field == "center_x") out = Circle.Center.X;
	else if (field == "center_y") out = Circle.Center.Y;
	else return false;
	return true;
}
#endif

void THole::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (!BallCapturedFlag)
	{
		BallCapturedSecondStage = false;
		Threshold = 1e9f;
		BallCapturedFlag = true;
		ball->CollisionComp = this;
		ball->Position.X = Circle.Center.X;
		ball->Position.Y = Circle.Center.Y;
		ball->Direction.Z = RiseSpeed; // 0 unless a Full Tilt table sets attr 1306
		HeldBall = ball;
		ball->CollisionDisabledFlag = true;

		if (pb::IsFullTiltTable())
		{
			// FT 0x4169c0: hold time from attr 407; below 0 no timer runs (the rules release the ball)
			if (Unknown3 >= 0.0f)
				Timer = timer::set(Unknown3, this, TimerExpired);
		}
		else
		{
			// Ramp hole has no delay in FT.
			auto captureTime = pb::FullTiltMode ? 0 : 0.5f;
			Timer = timer::set(captureTime, this, TimerExpired);
		}

		if (!PinballTable->TiltLockFlag)
		{
			loader::play_sound(HardHitSoundId, ball, "THole1");
			control::handler(MessageCode::ControlBallCaptured, this);
		}
	}
	else
	{
		DefaultCollision(ball, nextPosition, direction);
	}
}

int THole::FieldEffect(TBall* ball, vector2* vecDst)
{
	int result;
	vector2 direction{};

	if (BallCapturedFlag)
	{
		if (BallCapturedSecondStage)
		{
			const bool fullTilt = pb::IsFullTiltTable();
			if (RiseSpeed <= 0.0f)
				ball->Direction.Z -= PinballTable->GravityDirVectMult * ball->TimeDelta * GravityMult;
			else
				ball->Direction.Z = RiseSpeed;
			ball->Position.Z += ball->Direction.Z;
			if (fullTilt)
			{
				ball->Direction.X = 0.0f;
				ball->Direction.Y = 0.0f;
			}
			const bool atExit = RiseSpeed <= 0.0f ? ball->Position.Z <= ZSetValue : ball->Position.Z >= ZSetValue;
			if (atExit)
			{
				if (fullTilt)
				{
					// FT 0x416ab0: stop the ball, then release through Message 0x3f8 (exit check)
					ball->Direction = {};
					ball->Speed = 0.0f;
					Message(MessageCode::PlungerFeedBall, 0.0f);
				}
				else
				{
					BallCapturedFlag = false;
					BallCapturedSecondStage = false;
					ball->Position.Z = ZSetValue;
					ball->CollisionMask = CollisionMask;
					ball->CollisionComp = nullptr;
					ball->Direction = {};
					ball->Speed = 0.0f;
					loader::play_sound(SoftHitSoundId, ball, "THole2");
					control::handler(MessageCode::ControlBallReleased, this);
				}
			}
		}
		result = 0;
	}
	else
	{
		direction.X = Circle.Center.X - ball->Position.X;
		direction.Y = Circle.Center.Y - ball->Position.Y;
		if (direction.X * direction.X + direction.Y * direction.Y <= Circle.RadiusSq)
		{
			maths::normalize_2d(direction);
			vecDst->X = direction.X * GravityPull - ball->Direction.X * ball->Speed;
			vecDst->Y = direction.Y * GravityPull - ball->Direction.Y * ball->Speed;
			result = 1;
		}
		else
		{
			result = 0;
		}
	}
	return result;
}

void THole::TimerExpired(int timerId, void* caller)
{
	auto hole = static_cast<THole*>(caller);
	hole->Timer = 0;
	hole->BallCapturedSecondStage = true;
}
