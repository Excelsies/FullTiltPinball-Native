#pragma once
#include "TCircle.h"
#include "TCollisionComponent.h"
#include "TEdgeManager.h"
#include "DelayedMessage.h"

class THole :
	public TCollisionComponent
{
public:
	THole(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	void Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
	               TEdgeSegment* edge) override;
	int FieldEffect(TBall* ball, vector2* vecDst) override;

	static void TimerExpired(int timerId, void* caller);
#ifdef FT_TEST_HOOK
	bool HookGet(const std::string& field, double& out) const override;
#endif

	bool BallCapturedFlag{};
	bool BallCapturedSecondStage{};
	int Timer;
	float Unknown3; // hold time (attr 407); used by the Full Tilt tables only
	float Unknown4;
	float GravityMult;
	float ZSetValue;
	int CollisionMask;
	float GravityPull;
	circle_type Circle{};
	field_effect_type Field{};

	// Full Tilt tables only (gate pb::IsFullTiltTable()).
	float RiseSpeed{};              // attr 1306 (FT +0x92); 0 = the ball falls
	TBall* HeldBall{};              // FT +0xba
	DelayedMessage ReleaseRetry;    // FT +0xbe: 0x3f8 re-queued every 0.5 s while the exit is occupied

private:
	void SecondStageNow();          // FT 0x416a90
	int ReleaseHeldBall();          // FT Message 0x3f8
};
