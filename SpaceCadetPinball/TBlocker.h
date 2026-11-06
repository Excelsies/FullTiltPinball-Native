#pragma once
#include "TCollisionComponent.h"

class TBlocker :
	public TCollisionComponent
{
public:
	TBlocker(TPinballTable* table, int groupIndex);
	~TBlocker() override;
	int Message(MessageCode code, float value) override;
#ifdef FT_TEST_HOOK
	bool HookGet(const std::string& field, double& out) const override;
#endif

	static void TimerExpired(int timerId, void* caller);
	int FtCurrentState() const { return CurrentState; }	// FT +0x7e, read by the rules (PassRockAControl / PassRockBControl)

	int InitialDuration;
	int ExtendedDuration;
	int Timer;
	int SoundIndex4;
	int SoundIndex3;

private:
	// Full Tilt multi-state blocker (type 1011; FT 0x414d60 / 0x414f80). Used only when pb::IsFullTiltTable();
	// Space Cadet keeps the single-state upstream code above.
	static const int MaxStates = 8;

	int MessageFt(MessageCode code, float value);
	void SetFrame(MessageCode code, float value);
	int LastState() const;
	bool BallInTheWay() const;
	void KillStepTimer();
	void StartStepTimer(float delay);
	static void StepTimerExpired(int timerId, void* caller);
	static void DeferredMessageExpired(int timerId, void* caller);

	bool FtMode;
	char StateWall[MaxStates];	// per-state wall active flags (FT +0x76..+0x7d)
	int CurrentState;			// FT +0x7e, -1 = open
	int ResetState;				// attr 409, FT +0x82
	int StepTarget;				// FT +0x92
	float StepDelay;			// attr 903, FT +0x96
	int StepTimer;				// FT +0x9a
	int DeferredTimer;			// FT +0x9e..: message delayed while a ball is in the way
	MessageCode DeferredCode;
	float DeferredValue;
};
