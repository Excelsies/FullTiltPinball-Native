#include "pch.h"
#include "TTimer.h"

#include <algorithm>
#include <cmath>

#include "control.h"
#include "timer.h"

TTimer::TTimer(TPinballTable* table, int groupIndex) : TPinballComponent(table, groupIndex, true)
{
	Timer = 0;
}

int TTimer::TimeLeftMs() const
{
	if (!Timer)
		return 0;
	// FT 0x41b380: ftol(floor(max(timer::get_time_left(id), 0.0) * 1000.0)); an id that is not in the list gives -1.0 -> 0
	return static_cast<int>(std::floor(std::max(timer::get_time_left(Timer), 0.0f) * 1000.0));
}

int TTimer::Message(MessageCode code, float value)
{
	int result = Dispatch(code, value);
#ifdef FT_TEST_HOOK
	// records what the Message path really returned for 0x48 (a hook script checks it)
	if (code == MessageCode::TTimerGetTimeLeft)
		LastGetTimeLeftMs = result;
#endif
	return result;
}

int TTimer::Dispatch(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TTimerResetTimer:
		// FT 0x41b380 case 0x45: a negative value only stops the timer (upstream would fire at once).
		// FT keeps the stale id after the kill; zeroing it is equivalent (nothing finds it afterwards).
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		if (value >= 0.0f)
			Timer = timer::set(value, this, TimerExpired);
		break;
	case MessageCode::TTimerGetTimeLeft:
		// FT 0x41b380 case 0x48
		return TimeLeftMs();
	case MessageCode::SetTiltLock:
	case MessageCode::GameOver:
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		break;
	case MessageCode::Reset:
		// FT case 0x402: MessageField and the per-player copies = 0 (0x413550), then kill
		ResetPlayerState();
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		break;
	case MessageCode::PlayerChanged:
		// FT case 0x3fe: swap MessageField (0x413570), kill; the remaining time is not saved per player
		SwapPlayerState(value);
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		break;
	default: break;
	}
	return 0;
}

void TTimer::TimerExpired(int timerId, void* caller)
{
	auto timer = static_cast<TTimer*>(caller);
	timer->Timer = 0;
#ifdef FT_TEST_HOOK
	timer->ExpiredCount++;
#endif
	control::handler(MessageCode::ControlTimerExpired, timer);
}

#ifdef FT_TEST_HOOK
bool TTimer::HookGet(const std::string& field, double& out) const
{
	if (field == "timer") out = Timer != 0;
	else if (field == "time_left_ms") out = TimeLeftMs();
	else if (field == "last_get_ms") out = LastGetTimeLeftMs;
	else if (field == "expired") out = ExpiredCount;
	else return TPinballComponent::HookGet(field, out);
	return true;
}
#endif
