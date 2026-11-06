#include "pch.h"
#include "TBlocker.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "TEdgeSegment.h"
#include "TPinballTable.h"
#include "timer.h"

// On the Full Tilt tables the base constructor does not build the wall (FT 0x414d60 passes createWall = 0):
// each state gets its own wall below.
TBlocker::TBlocker(TPinballTable* table, int groupIndex) :
	TCollisionComponent(table, groupIndex, !pb::IsFullTiltTable()),
	FtMode(pb::IsFullTiltTable()), StateWall{}, CurrentState(-1), ResetState(-1), StepTarget(0),
	StepDelay(0.1f), StepTimer(0), DeferredTimer(0), DeferredCode(MessageCode::Reset), DeferredValue(0)
{
	visualStruct visual{};

	loader::query_visual(groupIndex, 0, &visual);
	SoundIndex4 = visual.SoundIndex4;
	SoundIndex3 = visual.SoundIndex3;
	InitialDuration = 55;
	ExtendedDuration = 5;
	Threshold = 1000000000.0f;
	Timer = 0;

	if (FtMode)
	{
		const int states = loader::query_visual_states(groupIndex);
		assertm(states <= MaxStates, "TBlocker: more than 8 states");
		for (int i = 0; i < states; i++)
		{
			// Optional per state: a_jaw / a_cannon have no wall on state 0 (FT 0x40d8d0 is the silent query).
			auto wall = loader::query_float_attribute_opt(groupIndex, i, 600);
			if (wall)
				TEdgeSegment::install_wall(wall, this, &StateWall[i], visual.CollisionGroup,
				                           table->CollisionCompOffset, 0);
		}
		if (auto durations = loader::query_float_attribute_opt(groupIndex, 0, 407))
		{
			InitialDuration = static_cast<int>(durations[0]);
			ExtendedDuration = static_cast<int>(durations[1]);
		}
		if (auto delay = loader::query_float_attribute_opt(groupIndex, 0, 903))
			StepDelay = delay[0];
		if (auto reset = loader::query_float_attribute_opt(groupIndex, 0, 409))
			ResetState = static_cast<int>(reset[0]);
		Message(MessageCode::Reset, 0.0f);
		return;
	}

	MessageField = 0;
	ActiveFlag = 0;
	SpriteSet(-1);
}

TBlocker::~TBlocker()
{
	if (FtMode && DeferredTimer)
		timer::kill(DeferredTimer);
}

int TBlocker::Message(MessageCode code, float value)
{
	if (FtMode)
		return MessageFt(code, value);

	switch (code)
	{
	case MessageCode::SetTiltLock:
	case MessageCode::PlayerChanged:
	case MessageCode::Reset:
	case MessageCode::TBlockerDisable:
		if (Timer)
		{
			timer::kill(Timer);
			Timer = 0;
		}
		MessageField = 0;
		ActiveFlag = 0;
		SpriteSet(-1);
		if (code == MessageCode::TBlockerDisable)
			loader::play_sound(SoundIndex3, this, "TBlocker1");
		break;
	case MessageCode::TBlockerEnable:
		ActiveFlag = 1;
		loader::play_sound(SoundIndex4, this, "TBlocker2");
		SpriteSet(0);
		if (Timer)
			timer::kill(Timer);
		Timer = 0;
		if (value >= 0)
			Timer = timer::set(value, this, TimerExpired);
		break;
	case MessageCode::TBlockerRestartTimeout:
		if (Timer)
			timer::kill(Timer);
		Timer = timer::set(std::max(value, 0.0f), this, TimerExpired);
		break;
	default:
		break;
	}
	
	return 0;
}

void TBlocker::TimerExpired(int timerId, void* caller)
{
	auto blocker = static_cast<TBlocker*>(caller);
	blocker->Timer = 0;
	control::handler(MessageCode::ControlTimerExpired, blocker);
}

int TBlocker::LastState() const
{
	return ListBitmap ? static_cast<int>(ListBitmap->size()) - 1 : -1;
}

bool TBlocker::BallInTheWay() const
{
	return PinballTable->BallCountInRect(AABB) != 0;
}

void TBlocker::KillStepTimer()
{
	if (StepTimer)
	{
		timer::kill(StepTimer);
		StepTimer = 0;
	}
}

void TBlocker::StartStepTimer(float delay)
{
	if (StepTimer)
		timer::kill(StepTimer);
	StepTimer = timer::set(delay, this, StepTimerExpired);
}

// FT 0x414f00: one state per period towards StepTarget; a ball in the way holds the move.
void TBlocker::StepTimerExpired(int timerId, void* caller)
{
	auto blocker = static_cast<TBlocker*>(caller);
	blocker->StepTimer = 0;
	if (blocker->CurrentState == blocker->StepTarget)
	{
		control::handler(MessageCode::ControlBlockerMoveDone, blocker);
		return;
	}
	if (!blocker->BallInTheWay())
	{
		const int next = blocker->CurrentState + (blocker->StepTarget > blocker->CurrentState ? 1 : -1);
		blocker->Message(MessageCode::TBlockerSetFrame, static_cast<float>(next));
	}
	blocker->StepTimer = timer::set(blocker->StepDelay, blocker, StepTimerExpired);
}

// FT 0x4131e0: the delayed message is delivered once, 0.5 s after it was held back.
void TBlocker::DeferredMessageExpired(int timerId, void* caller)
{
	auto blocker = static_cast<TBlocker*>(caller);
	blocker->DeferredTimer = 0;
	blocker->Message(blocker->DeferredCode, blocker->DeferredValue);
}

// Full Tilt TBlocker::Message (FT 0x414f80).
int TBlocker::MessageFt(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TBlockerRestartTimeout:
		if (Timer)
			timer::kill(Timer);
		Timer = timer::set(std::max(value, 0.0f), this, TimerExpired);
		return 0;
	case MessageCode::TBlockerDisable:
		// The value is the delay before the move starts (not a timeout length as in upstream).
		if (value > 0.0f)
		{
			StepTarget = -1;
			StartStepTimer(value);
		}
		else
			Message(MessageCode::TBlockerMoveToFrame, -1.0f);
		loader::play_sound(SoundIndex3, this, "TBlocker1");
		return 0;
	case MessageCode::TBlockerEnable:
		if (value > 0.0f)
		{
			StepTarget = LastState();
			StartStepTimer(value);
		}
		else
			Message(MessageCode::TBlockerMoveToFrame, static_cast<float>(LastState()));
		loader::play_sound(SoundIndex4, this, "TBlocker2");
		return 0;
	case MessageCode::TBlockerMoveToFrame:
		StepTarget = static_cast<int>(std::floor(value));
		StartStepTimer(StepDelay);
		return 0;
	case MessageCode::TBlockerEnableNow:
		Message(MessageCode::TBlockerSetFrame, static_cast<float>(LastState()));
		loader::play_sound(SoundIndex4, this, "TBlocker2");
		return 0;
	case MessageCode::TBlockerDisableNow:
		Message(MessageCode::TBlockerSetFrame, -1.0f);
		loader::play_sound(SoundIndex3, this, "TBlocker1");
		return 0;
	case MessageCode::TBlockerSetFrame:
		SetFrame(code, value);
		return 0;
	case MessageCode::PlayerChanged:
		SwapPlayerState(value);
		break;
	case MessageCode::Reset:
		ResetPlayerState();
		break;
	case MessageCode::SetTiltLock:
		break;
	default:
		return 0;
	}

	// SetTiltLock, PlayerChanged, Reset: stop both timers and go to the reset state.
	if (Timer)
	{
		timer::kill(Timer);
		Timer = 0;
	}
	KillStepTimer();
	Message(MessageCode::TBlockerSetFrame, static_cast<float>(ResetState));
	return 0;
}

// FT 0x414f80 case 0x39: jump to state n at once; held back 0.5 s per try while a ball is in the way.
void TBlocker::SetFrame(MessageCode code, float value)
{
	const int state = static_cast<int>(std::floor(value));
	if (DeferredTimer)
	{
		timer::kill(DeferredTimer);
		DeferredTimer = 0;
	}
	KillStepTimer();
	if (state >= 0 && BallInTheWay())
	{
		DeferredCode = code;
		DeferredValue = value;
		DeferredTimer = timer::set(0.5f, this, DeferredMessageExpired);
		return;
	}

	assertm(state < MaxStates, "TBlocker: state out of range");
	const int last = LastState();
	if (CurrentState >= (ListBitmap ? last + 1 : 0))
		CurrentState = last;
	if (CurrentState >= 0)
		StateWall[CurrentState] = 0;
	CurrentState = state;
	if (state >= 0)
		StateWall[state] = 1;
	SpriteSet(state >= 0 && state <= last ? state : -1);
}

#ifdef FT_TEST_HOOK
bool TBlocker::HookGet(const std::string& field, double& out) const
{
	if (FtMode && field == "blocker_state")
	{
		out = CurrentState;
		return true;
	}
	return TCollisionComponent::HookGet(field, out);
}
#endif
