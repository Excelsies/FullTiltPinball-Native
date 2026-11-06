#pragma once
#include "TCollisionComponent.h"

// Full Tilt part type 1043: a one-way wall of one or more segments that the table rules switch on and off
// (DRAGON.EXE ctor 0x41bb70, Message 0x41be60, Collision 0x41bc90, timer 0x41bc50).
// Front side (TLine, WallValue 0) bounces the ball; back side (WallValue 1) lets it through and flashes bitmap 1.
class TDiverter : public TCollisionComponent
{
public:
	TDiverter(TPinballTable* table, int groupIndex);
	~TDiverter() override;
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;
#ifdef FT_TEST_HOOK
	bool HookGet(const std::string& field, double& out) const override;
#endif

	static void TimerExpired(int timerId, void* caller);

	float FlashTime;		// attr 407, default 0.1 s (FT +0x82)
	int EnableSoundId;		// FT +0x76, never written by the original: 0
	int DisableSoundId;		// FT +0x7a, never written by the original: 0
	char PlayerActive[4];	// per-player saved ActiveFlag (FT +0x86)

private:
	void AddTwoSidedLine(const float* pts, unsigned int collisionGroup);
};
