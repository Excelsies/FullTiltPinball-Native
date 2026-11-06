#pragma once
#include "TCollisionComponent.h"
#include "TEdgeManager.h"

// Full Tilt part type 1036 ("a_accl*"): an invisible line the ball passes through. The ball is
// stopped for the rest of the frame and, in the next frame's field-effect pass, thrown along the
// kicker record's direction at the kicker's Boost speed.
class TAccelerator : public TCollisionComponent
{
public:
	TAccelerator(TPinballTable* table, int groupIndex);
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;
	int FieldEffect(TBall* ball, vector2* vecDst) override;
#ifdef FT_TEST_HOOK
	bool HookGet(const std::string& field, double& out) const override;
#endif

	TBall* Ball{};
	vector3 BallThrowDirection{};
	float ThrowAngleMult{};
	float ThrowSpeedMult1{};
	float ThrowSpeedMult2{};
	int HitSoundId{};
	field_effect_type Field{};
};
