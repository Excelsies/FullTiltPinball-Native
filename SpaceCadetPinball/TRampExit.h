#pragma once
#include "TCollisionComponent.h"

class TLine;

// Full Tilt part type 1035 ("ramp exit"): an invisible two-way collision-layer switch line.
// A ball crossing it keeps its position along the ray and its direction; only its collision mask
// changes (layer A <-> layer B), and the table rules are told which way it went.
class TRampExit : public TCollisionComponent
{
public:
	TRampExit(TPinballTable* table, int groupIndex);
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;
#ifdef FT_TEST_HOOK
	bool HookGet(const std::string& field, double& out) const override;
#endif

	TLine* LineToB{};       // P0 -> P1, group A: a layer-A ball crossing it moves to layer B
	int CollisionGroupB{};  // 1 << floor(attr1301[1])
	TLine* LineToA{};       // P1 -> P0, group B: a layer-B ball crossing it moves to layer A
	int CollisionGroupA{};  // 1 << floor(attr1301[0])
};
