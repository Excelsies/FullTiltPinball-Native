#pragma once

enum class MessageCode;
class TPinballComponent;

// Full Tilt delayed-message helper (0x4131a0 Set / 0x413180 Kill / callback 0x4131e0): sends one
// message to a component after 0.5 s; a new Set replaces a pending one.
struct DelayedMessage
{
	void Set(TPinballComponent* target, MessageCode code, float value);
	void Kill();
	int TimerId = 0;
	MessageCode Code{};
	float Value = 0;
	TPinballComponent* Target = nullptr;

private:
	static void TimerExpired(int timerId, void* caller);
};
