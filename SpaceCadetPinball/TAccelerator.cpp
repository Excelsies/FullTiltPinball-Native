#include "pch.h"
#include "TAccelerator.h"

#include "control.h"
#include "loader.h"
#include "TBall.h"
#include "TPinballTable.h"
#include "TTableLayer.h"

TAccelerator::TAccelerator(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	visualStruct visual{};
	loader::query_visual(groupIndex, 0, &visual);
	BallThrowDirection = visual.Kicker.ThrowBallDirection;
	ThrowAngleMult = visual.Kicker.ThrowBallAngleMult;
	ThrowSpeedMult1 = visual.Kicker.Boost;
	ThrowSpeedMult2 = visual.Kicker.ThrowBallMult / 100.0f;
	HitSoundId = visual.SoundIndex4;

	auto radius = (AABB.YMax - AABB.YMin) / 1.8f;
	auto radiusX = (AABB.XMax - AABB.XMin) / 1.8f;
	if (radius <= radiusX)
		radius = radiusX;
	circle_type circle{};
	circle.Center.X = (AABB.XMin + AABB.XMax) / 2.0f;
	circle.Center.Y = (AABB.YMax + AABB.YMin) / 2.0f;
	circle.RadiusSq = radius * radius;
	Field.ActiveFlag = &ActiveFlag;
	Field.CollisionGroup = visual.CollisionGroup;
	Field.CollisionComp = this;
	TTableLayer::edges_insert_circle(&circle, nullptr, &Field);
}

void TAccelerator::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                             TEdgeSegment* edge)
{
	ball->not_again(edge);
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;
	if (!PinballTable->TiltLockFlag)
	{
		Ball = ball;
		ball->CollisionDisabledFlag = true;
		loader::play_sound(HitSoundId, ball, "TAccelerator");
		control::handler(MessageCode::ControlCollision, this);
	}
}

int TAccelerator::FieldEffect(TBall* ball, vector2* vecDst)
{
	if (Ball != ball)
		return 0;
	Ball = nullptr;
	ball->throw_ball(&BallThrowDirection, ThrowAngleMult, ThrowSpeedMult1, ThrowSpeedMult2);
	return 0;
}

#ifdef FT_TEST_HOOK
bool TAccelerator::HookGet(const std::string& field, double& out) const
{
	if (field == "speed_mult1") { out = ThrowSpeedMult1; return true; }
	if (field == "speed_mult2") { out = ThrowSpeedMult2; return true; }
	if (field == "dir_x") { out = BallThrowDirection.X; return true; }
	if (field == "dir_y") { out = BallThrowDirection.Y; return true; }
	return TCollisionComponent::HookGet(field, out);
}
#endif
