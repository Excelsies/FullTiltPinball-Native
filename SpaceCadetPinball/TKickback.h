#pragma once
#include "TCollisionComponent.h"

class TKickback :
	public TCollisionComponent
{
public:
	TKickback(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
		TEdgeSegment* edge) override;

#ifdef FT_TEST_HOOK
	bool HookGet(const std::string& field, double& out) const override;
#endif

	static void TimerExpired(int timerId, void* caller);
	// Full Tilt tables (0x415c60 / 0x415ce0): arming timer and end-of-kick timer.
	static void FtTimerArmed(int timerId, void* caller);
	static void FtTimerEnd(int timerId, void* caller);

	float TimerTime;
	float TimerTime2;
	int Timer;
	int KickActiveFlag;
	int DisabledFlag; // FT +0x86: set by FtPartDisable, cleared by every reset

private:
	void FtReset(); // FT Message 0x415d70 tail: clear flags, kill timer, sprite bitmap 0
};
