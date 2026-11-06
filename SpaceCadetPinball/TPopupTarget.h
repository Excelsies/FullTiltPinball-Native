#pragma once
#include "TCollisionComponent.h"

class TPopupTarget :
	public TCollisionComponent
{
public:
	TPopupTarget(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
		TEdgeSegment* edge) override;	

#ifdef FT_TEST_HOOK
	bool HookGet(const std::string& field, double& out) const override;
#endif

	static void TimerExpired(int timerId, void* caller);

	int Timer;
	float TimerTime;
	int PlayerMessagefieldBackup[4]{};
	// Full Tilt tables only (pb::IsFullTiltTable() at construction; FT 0x4148c0..0x414a00):
	// ball-in-AABB retry on raise, Disable kills the raise timer, per-player ActiveFlag (FT +0x7e, 4 bytes).
	bool FtMode;
	char PlayerActiveBackup[4]{};
};
