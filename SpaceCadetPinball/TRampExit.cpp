#include "pch.h"
#include "TRampExit.h"

#include "control.h"
#include "loader.h"
#include "TBall.h"
#include "TLine.h"
#include "TPinballTable.h"

// FT 0x41b4f0. With no attribute 1301, or a point count other than 2, the part has no edges.
TRampExit::TRampExit(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	auto floatArr = loader::query_float_attribute_opt(groupIndex, 0, 1301);
	if (floatArr == nullptr || floatArr[2] != 2.0f)
		return;

	CollisionGroupA = 1 << (static_cast<int>(floor(floatArr[0])) & 0x1f);
	CollisionGroupB = 1 << (static_cast<int>(floor(floatArr[1])) & 0x1f);
	vector2 p0{floatArr[3], floatArr[4]};
	vector2 p1{floatArr[5], floatArr[6]};

	// Both lines lie on the same boundary, offset by the ball radius, facing opposite ways.
	LineToB = new TLine(this, &ActiveFlag, CollisionGroupA, p0, p1);
	LineToB->Offset(table->CollisionCompOffset);
	LineToB->place_in_grid(&AABB);
	EdgeList.push_back(LineToB);

	LineToA = new TLine(this, &ActiveFlag, CollisionGroupB, p1, p0);
	LineToA->Offset(-table->CollisionCompOffset);
	LineToA->place_in_grid(&AABB);
	EdgeList.push_back(LineToA);
}

// FT 0x41b6b0. No bounce, no tilt-lock test: the ball is carried to the hit point, the table rules
// get the crossing message (before the mask changes), then the ball's frame ends on the boundary.
void TRampExit::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                          TEdgeSegment* edge)
{
	ball->not_again(edge);
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;
	if (edge == LineToB)
	{
		control::handler(MessageCode::ControlBallToLayer2, this);
		ball->CollisionMask = CollisionGroupB;
	}
	else
	{
		control::handler(MessageCode::ControlBallToLayer1, this);
		ball->CollisionMask = CollisionGroupA;
	}
	ball->CollisionDisabledFlag = true;
}

#ifdef FT_TEST_HOOK
bool TRampExit::HookGet(const std::string& field, double& out) const
{
	if (field == "group_a") { out = CollisionGroupA; return true; }
	if (field == "group_b") { out = CollisionGroupB; return true; }
	return TCollisionComponent::HookGet(field, out);
}
#endif
