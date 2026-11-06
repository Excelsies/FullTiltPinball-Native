#pragma once
#include "TPinballComponent.h"

class TTimer :
	public TPinballComponent
{
public:
	TTimer(TPinballTable* table, int groupIndex);
	int Message(MessageCode code, float value) override;
	static void TimerExpired(int timerId, void* caller);
#ifdef FT_TEST_HOOK
	bool HookGet(const std::string& field, double& out) const override;
#endif

	int Timer;

private:
	// FT 0x41b380 case 0x48: milliseconds left on the running timer, 0 when none (and when already due).
	int TimeLeftMs() const;
	int Dispatch(MessageCode code, float value);
#ifdef FT_TEST_HOOK
	int ExpiredCount = 0;
	// value returned by the last Message(TTimerGetTimeLeft), -1 before the first
	int LastGetTimeLeftMs = -1;
#endif
};
