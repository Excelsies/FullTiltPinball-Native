#pragma once
#include "TKickout.h"

// Full Tilt part type 1042: a playfield magnet (DRAGON.EXE ctor 0x416560, Message 0x4166c0, Collision 0x4165a0,
// timer callback 0x4166a0). Built on TKickout with someFlag = 1: the inherited circular
// field (FieldEffect 0x4162e0, unchanged) pulls the ball while HoldOff (BallCaputeredFlag, FT +0x76) is 0, and the
// small centre trigger circle lets the ball pass through, switching the field off for TimerTime2 (FT +0x86, 1.0 s).
class TMagnet : public TKickout
{
public:
	TMagnet(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;
#ifdef FT_TEST_HOOK
	bool HookGet(const std::string& field, double& out) const override;
#endif

	static void TimerExpired(int timerId, void* caller);

private:
	void Disable();
};
