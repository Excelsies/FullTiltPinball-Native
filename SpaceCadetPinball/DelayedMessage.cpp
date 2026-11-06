#include "pch.h"
#include "DelayedMessage.h"

#include "timer.h"
#include "TPinballComponent.h"

void DelayedMessage::Set(TPinballComponent* target, MessageCode code, float value)
{
	Kill();
	Target = target;
	Code = code;
	Value = value;
	TimerId = timer::set(0.5f, this, TimerExpired);
}

void DelayedMessage::Kill()
{
	if (TimerId)
		timer::kill(TimerId);
	TimerId = 0;
}

void DelayedMessage::TimerExpired(int, void* caller)
{
	auto self = static_cast<DelayedMessage*>(caller);
	self->TimerId = 0;
	self->Target->Message(self->Code, self->Value);
}
