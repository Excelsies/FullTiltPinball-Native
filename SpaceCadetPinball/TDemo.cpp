#include "pch.h"
#include "TDemo.h"


#include "loader.h"
#include "pb.h"
#include "TEdgeSegment.h"
#include "timer.h"
#include "TPinballTable.h"
#include "TBall.h"
#include "nudge.h"
#include "TPlunger.h"

TDemo::TDemo(TPinballTable* table, int groupIndex)
	: TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};

	RestartGameTimer = 0;
	PlungerFlag = 0;
	FlipLeftTimer = 0;
	FlipRightTimer = 0;
	MessageField = 0;
	SavedBallPosition = {0.0f, 0.0f};
	UnusedBaseFlag = 0;
	ActiveFlag = 0;
	FlipRightFlag = 0;
	FlipLeftFlag = 0;
	table->Demo = this;
	auto floatArr1 = loader::query_float_attribute(groupIndex, 0, 407);
	if (floatArr1)
	{
		FlipTimerTime1 = floatArr1[0];
		FlipTimerTime2 = floatArr1[1];
		UnFlipTimerTime1 = floatArr1[2];
		UnFlipTimerTime2 = floatArr1[3];
	}
	else
	{
		FlipTimerTime1 = 0.2f;
		FlipTimerTime2 = 0.1f;
		UnFlipTimerTime1 = 0.2f;
		UnFlipTimerTime2 = 0.1f;
	}
	loader::query_visual(groupIndex, 0, &visual);
	auto v5 = loader::query_float_attribute(groupIndex, 0, 1400);
	Edge1 = TEdgeSegment::install_wall(v5, this, &ActiveFlag, visual.CollisionGroup, 0.0, 1400);

	auto v6 = loader::query_float_attribute(groupIndex, 0, 1401);
	TEdgeSegment::install_wall(v6, this, &ActiveFlag, visual.CollisionGroup, 0.0, 1401);

	auto v7 = loader::query_float_attribute(groupIndex, 0, 1402);
	Edge2 = TEdgeSegment::install_wall(v7, this, &ActiveFlag, visual.CollisionGroup, 0.0, 1402);

	auto v8 = loader::query_float_attribute(groupIndex, 0, 1403);
	TEdgeSegment::install_wall(v8, this, &ActiveFlag, visual.CollisionGroup, 0.0, 1403);

	auto v9 = loader::query_float_attribute(groupIndex, 0, 1404);
	Edge3 = TEdgeSegment::install_wall(v9, this, &ActiveFlag, visual.CollisionGroup, table->CollisionCompOffset, 1404);

	// FT 0x417a50: optional extra trigger walls 1405 / 1406 act as the 1401 / 1403 walls on
	// another collision layer: first float = layer n (mask 1 << n), the wall follows. Absent in CADET.DAT.
	auto v10 = loader::query_float_attribute_opt(groupIndex, 0, 1405);
	if (v10)
		TEdgeSegment::install_wall(v10 + 1, this, &ActiveFlag, 1u << static_cast<int>(v10[0]), 0.0, 1401);
	auto v11 = loader::query_float_attribute_opt(groupIndex, 0, 1406);
	if (v11)
		TEdgeSegment::install_wall(v11 + 1, this, &ActiveFlag, 1u << static_cast<int>(v11[0]), 0.0, 1403);
}

int TDemo::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::NewGame:
		if (RestartGameTimer)
			timer::kill(RestartGameTimer);
		RestartGameTimer = 0;
		if (pb::IsFullTiltTable())
		{
			// FT 0x418290 case 0x3f6: the watchdog starts with the ball position (-100, -100)
			SavedBallPosition = {-100.0f, -100.0f};
			timer::kill(Watchdog);
			if (ActiveFlag)
				timer::set(WatchdogPeriod, this, Watchdog);
		}
		break;
	case MessageCode::GameOver:
		if (RestartGameTimer)
			timer::kill(RestartGameTimer);
		RestartGameTimer = 0;
		if (ActiveFlag != 0)
			RestartGameTimer = timer::set(5.0, this, NewGameRestartTimer);
		if (pb::IsFullTiltTable())
			timer::kill(Watchdog); // FT 0x418290 case 0x400
		break;
	case MessageCode::Reset:
		if (FlipLeftTimer)
			timer::kill(FlipLeftTimer);
		FlipLeftTimer = 0;
		if (FlipRightTimer)
			timer::kill(FlipRightTimer);
		FlipRightTimer = 0;

		if (FlipLeftFlag != 0)
			UnFlipLeft(0, this);
		if (FlipRightFlag)
			UnFlipRight(0, this);
		if (PlungerFlag)
			PlungerRelease(0, this);
		break;
	default:
		break;
	}
	return 0;
}

void TDemo::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	ball->not_again(edge);
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;

	switch (reinterpret_cast<size_t>(edge->WallValue))
	{
	case 1400:
		if (!FlipLeftTimer && !FlipLeftFlag)
		{
			float time = FlipTimerTime1 + FlipTimerTime2 - RandFloat() * (FlipTimerTime2 + FlipTimerTime2);
			FlipLeftTimer = timer::set(time, this, FlipLeft);
		}
		break;
	case 1401:
		FlipLeft(0, this);
		break;
	case 1402:
		if (!FlipRightTimer && !FlipRightFlag)
		{
			float time = FlipTimerTime1 + FlipTimerTime2 - RandFloat() * (FlipTimerTime2 + FlipTimerTime2);
			FlipRightTimer = timer::set(time, this, FlipRight);
		}
		break;
	case 1403:
		FlipRight(0, this);
		break;
	case 1404:
		if (!PlungerFlag)
		{
			PinballTable->Message(MessageCode::PlungerInputPressed, 0);
			float time = RandFloat() + 2.0f;
			PlungerFlag = timer::set(time, this, PlungerRelease);
		}
		break;
	default:
		break;
	}
}

void TDemo::PlungerRelease(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	demo->PlungerFlag = 0;
	demo->PinballTable->Message(MessageCode::PlungerInputReleased, pb::time_next);
}

void TDemo::UnFlipRight(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	if (demo->FlipRightFlag)
		demo->PinballTable->Message(MessageCode::RightFlipperInputReleased, pb::time_next);
	demo->FlipRightFlag = 0;
}

void TDemo::UnFlipLeft(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	if (demo->FlipLeftFlag)
		demo->PinballTable->Message(MessageCode::LeftFlipperInputReleased, pb::time_next);
	demo->FlipLeftFlag = 0;
}

void TDemo::FlipRight(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	if (!demo->FlipRightFlag)
	{
		if (demo->FlipRightTimer)
		{
			timer::kill(demo->FlipRightTimer);
			demo->FlipRightTimer = 0;
		}
		demo->PinballTable->Message(MessageCode::RightFlipperInputPressed, pb::time_next);
		demo->FlipRightFlag = 1;
		float time = demo->UnFlipTimerTime1 + demo->UnFlipTimerTime2 - RandFloat() *
			(demo->UnFlipTimerTime2 + demo->UnFlipTimerTime2);
		timer::set(time, demo, UnFlipRight);
	}
}

void TDemo::FlipLeft(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	if (!demo->FlipLeftFlag)
	{
		if (demo->FlipLeftTimer)
		{
			timer::kill(demo->FlipLeftTimer);
			demo->FlipLeftTimer = 0;
		}
		demo->PinballTable->Message(MessageCode::LeftFlipperInputPressed, pb::time_next);
		demo->FlipLeftFlag = 1;
		float time = demo->UnFlipTimerTime1 + demo->UnFlipTimerTime2 - RandFloat() *
			(demo->UnFlipTimerTime2 + demo->UnFlipTimerTime2);
		timer::set(time, demo, UnFlipLeft);
	}
}

void TDemo::NewGameRestartTimer(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	pb::replay_level(true);
	demo->PinballTable->Message(MessageCode::NewGame, static_cast<float>(demo->PinballTable->PlayerCount));
	demo->RestartGameTimer = 0;
}

// FT 0x417c40 (DRAGON export): every 5 s while the demo is active. Looks for the first ball
// staying (+-3 x CollisionCompOffset) around where it was last tick: inside the plunger's box it
// presses the plunger and arms the release timer, elsewhere it flips both flippers and nudges.
void TDemo::Watchdog(int timerId, void* caller)
{
	auto demo = static_cast<TDemo*>(caller);
	auto table = demo->PinballTable;
	if (!demo->ActiveFlag)
		return;

	if (!table->BallList.empty())
	{
		auto ball = table->BallList[0];
		auto offset = table->CollisionCompOffset;
		RectF watchBox{};
		watchBox.XMax = offset * 3.0f + demo->SavedBallPosition.X;
		watchBox.YMax = offset * 3.0f + demo->SavedBallPosition.Y;
		watchBox.XMin = offset * -3.0f + demo->SavedBallPosition.X;
		watchBox.YMin = offset * -3.0f + demo->SavedBallPosition.Y;
		if (table->BallCountInRect(watchBox) != 0)
		{
			if (BallInsidePlunger(ball, table->Plunger))
			{
				if (!demo->PlungerFlag)
				{
					table->Message(MessageCode::PlungerInputPressed, 0);
					float time = RandFloat() + 2.0f;
					demo->PlungerFlag = timer::set(time, demo, PlungerRelease);
				}
			}
			else
			{
				FlipRight(0, demo);
				FlipLeft(0, demo);
				ft_rand::Int(); // FT 0x42c4cb: a draw whose value is discarded
				nudge::nudge_right(); // FT 0x411eb0
			}
		}
		demo->SavedBallPosition = ball->Position;
	}
	if (demo->ActiveFlag)
		timer::set(WatchdogPeriod, demo, Watchdog);
}

// FT 0x412ae0: active ball inside the component's bounding box grown by CollisionCompOffset / 2.
bool TDemo::BallInsidePlunger(TBall* ball, TCollisionComponent* plunger)
{
	auto margin = plunger->PinballTable->CollisionCompOffset / 2.0f;
	return ball->ActiveFlag &&
		plunger->AABB.XMin - margin <= ball->Position.X &&
		plunger->AABB.YMin - margin <= ball->Position.Y &&
		ball->Position.X <= plunger->AABB.XMax + margin &&
		ball->Position.Y <= plunger->AABB.YMax + margin;
}
