#include "pch.h"
#include "rules/dragon_rules.h"
#include "TBlocker.h"
#include "TDiverter.h"
#include "TKickout.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TLightRollover.h"
#include "TPinballTable.h"
#include "TSink.h"
#include "TSoloTarget.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "TTripwire.h"
#include "table_text.h"
#include <cstdio>

// Dragon's Keep rules: the tower locks (reset / update / enable), the tower-lock target bank and its three
// targets, the window rollovers and tripwires, and the mode 13 and mode 14
// controllers. Port of DRAGON.EXE 0x423b80, 0x423be0, 0x423d50, 0x4250e0, 0x425220-0x425280, 0x427860-0x427d10,
// 0x42b0e0, 0x42b3c0 (port_listing_dragon.c lines 760-856, 1618-1710, 3694-3902, 5573-5692).
namespace dragon {
namespace {
// The original's get_rc_string + TTextBox::Display(box, text, time, 0, 0). A string the EXE does not provide shows nothing
// (the reader has logged why; same as DragonRules::TextEvent), the rule state still advances.
void ShowText(int id, float time)
{
	const char* text = table_text::Get(id);
	if (!text)
	{
		std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), shows nothing\n", id);
		return;
	}
	info_text_box->Display(text, time, 0, false);
}

// get_rc_string(id) as the format of crt_sprintf(buf[64], format, value), then Display(buf, 2.0).
// The format is the game's own data (window tripwire "%ld"-style award text).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
void ShowValueText(int id, int value)
{
	const char* format = table_text::Get(id);
	if (!format)
	{
		std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), shows nothing\n", id);
		return;
	}
	char buffer[64];
	std::snprintf(buffer, sizeof buffer, format, static_cast<long>(value));
	info_text_box->Display(buffer, 2.0f, 0, false);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// 0x4230a0 / 0x4230f0 return what they added to the score (the AddScore result), which the original passes to sprintf.
// dragon::ScorePart / ScoreValue return nothing, so the amount is read back from the running score.
long long RunningScore()
{
	TPinballTable* table = control::TableG;
	return static_cast<long long>(table->CurScoreE9) * 1000000000LL + table->CurScore;
}

int ScorePartAdded(TPinballComponent* comp, int index)
{
	const long long before = RunningScore();
	ScorePart(comp, index);
	return static_cast<int>(RunningScore() - before);
}

int ScoreValueAdded(TPinballComponent* comp, int score)
{
	const long long before = RunningScore();
	ScoreValue(comp, score);
	return static_cast<int>(RunningScore() - before);
}

void PlaySound(TSound* sound, const char* name)
{
	sound->Play(nullptr, name);
}

// The three tripwire functions share the tail "score by lit-window count, show it, advance the window lights".
void WindowTail(TLightGroup* group, TPinballComponent* caller)
{
	const int lit = group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	const int added = ScorePartAdded(caller, lit);
	ShowValueText(3008, added);
	const int count = group->Message(MessageCode::TLightGroupGetLightCount, 0.0f);
	if (count - lit == 1)
	{
		group->Message(MessageCode::TLightTurnOff, 0.0f);
		group->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		return;
	}
	group->Message(MessageCode::TLightGroupResetAndTurnOn, 1.0f);
}

// Mode 3 with the mode timer at 6, shared by the three tripwires: one plain score of the part, shown.
void WindowScoreOnly(TPinballComponent* caller)
{
	ShowValueText(3008, ScorePartAdded(caller, 0));
}
}

void ResetTowerLocks() // 0x423b80
{
	v_lite48->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
	v_lite49->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
	v_lite50->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
	a_sink18->ActiveFlag = 0;
	a_sink17->ActiveFlag = 0;
	v_lite48->MessageField = 0;
	v_lite49->MessageField = 0;
	v_lite50->MessageField = 0;
}

void UpdateTowerLocks() // 0x423be0
{
	v_lite48->Message(MessageCode::TLightFtResetOverride, 0.0f);
	v_lite49->Message(MessageCode::TLightFtResetOverride, 0.0f);
	v_lite50->Message(MessageCode::TLightFtResetOverride, 0.0f);
	if (v_lite48->FlasherOnFlag)
	{
		a_sink18->ActiveFlag = 1;
		a_sink17->ActiveFlag = 0;
		v_lite48->MessageField = 1;
		v_lite49->MessageField = 0;
		v_lite50->MessageField = 0;
		return;
	}
	if (v_lite49->FlasherOnFlag)
	{
		a_sink18->ActiveFlag = 0;
		a_sink17->ActiveFlag = 1;
		v_lite48->MessageField = 0;
		v_lite49->MessageField = 1;
		v_lite50->MessageField = 0;
		return;
	}
	if (v_lite50->FlasherOnFlag)
	{
		a_sink18->ActiveFlag = 0;
		a_sink17->ActiveFlag = 0;
		v_lite48->MessageField = 0;
		v_lite49->MessageField = 0;
		v_lite50->MessageField = 1;
		return;
	}
	a_sink18->ActiveFlag = 0;
	a_sink17->ActiveFlag = 0;
	v_lite48->MessageField = 0;
	v_lite49->MessageField = 0;
	v_lite50->MessageField = 0;
	if (v_lite48->light_on() && v_lite49->light_on() && v_lite50->light_on())
	{
		// The original writes the mode unconditionally; reproduced as is.
		SetMode(13);
		MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
	}
}

namespace {
void EnableTowerLock() // 0x423d50
{
	v_lite47->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	a_dvtraa->Message(MessageCode::TGateDisable, 0.0f);
	a_dvtrab->Message(MessageCode::TGateEnable, 0.0f);
	v_lite125->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	v_lite126->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_dvtrda->Message(MessageCode::TGateDisable, 0.0f);
	a_dvtrdb->Message(MessageCode::TGateEnable, 0.0f);
	if (!v_lite48->light_on())
		v_lite48->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	else if (!v_lite49->light_on())
		v_lite49->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	else if (!v_lite50->light_on())
		v_lite50->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	UpdateTowerLocks();
}

void TowerLockTargetBank(TPinballComponent* caller) // 0x4250e0
{
	if (!v_lite8->light_on() || !v_lite9->light_on() || !v_lite10->light_on())
	{
		if (Mode() != 4)
		{
			ScorePart(caller, 0);
			PlaySound(tabc06wave, "TowerLockTargetBank");
		}
		return;
	}
	v_lite8->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	v_lite9->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	v_lite10->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	ScorePart(caller, 1);
	if (Mode() == 2)
	{
		v_lite27->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		if (CheckMode15Start() == 0)
		{
			EnableTowerLock();
			PlaySound(tabc07wave, "TowerLockTargetBank");
			ShowText(3003, 2.0f);
		}
	}
	if (Mode() != 4)
		PlaySound(tabc06wave, "TowerLockTargetBank");
}
}

void TowerLockTargetControl1(MessageCode code, TPinballComponent* caller) // 0x425220, a_spot3
{
	if (code == MessageCode::ControlCollision)
	{
		v_lite8->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		TowerLockTargetBank(caller);
	}
}

void TowerLockTargetControl2(MessageCode code, TPinballComponent* caller) // 0x425250, a_spot2
{
	if (code == MessageCode::ControlCollision)
	{
		v_lite9->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		TowerLockTargetBank(caller);
	}
}

void TowerLockTargetControl3(MessageCode code, TPinballComponent* caller) // 0x425280, a_spot1
{
	if (code == MessageCode::ControlCollision)
	{
		v_lite10->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		TowerLockTargetBank(caller);
	}
}

void WindowRolloverControl(MessageCode code, TPinballComponent* caller) // 0x427860
{
	if (code == MessageCode::ControlCollision)
		ScorePart(caller, 0);
}

void PrisonWindowTripwireControl(MessageCode code, TPinballComponent* caller) // 0x427880
{
	if (code != MessageCode::ControlCollision)
		return;
	switch (Mode())
	{
	case 2:
		if (v_lite48->MessageField != 0)
			return;
		PlaySound(tabc04wave, "PrisonWindowTripwireControl");
		break;
	case 3:
		if (mode_timer->MessageField == 6)
		{
			WindowScoreOnly(caller);
			return;
		}
		break;
	case 6:
	{
		PlaySound(tabc51wave, "PrisonWindowTripwireControl");
		const int lit = prison_window_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		if (lit == 0)
			ShowValueText(3008, ScoreValueAdded(caller, 250000));
		else if (lit == 1)
			ShowValueText(3008, ScoreValueAdded(caller, 500000));
		else if (lit == 2)
			AwardJackpot(0.0f, 1);
		else
		{
			v_lite57->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			ShowText(3034, 2.0f);
		}
		prison_window_light_group->Message(MessageCode::TLightGroupResetAndTurnOn, 1.0f);
		return;
	}
	case 13:
		ScoreValue(caller, 100000);
		return;
	default:
		break;
	}
	WindowTail(prison_window_light_group, caller);
}

void WatchWindowTripwireControl(MessageCode code, TPinballComponent* caller) // 0x427b30
{
	if (code != MessageCode::ControlCollision)
		return;
	switch (Mode())
	{
	case 2:
		if (v_lite49->MessageField != 0)
			return;
		break;
	case 3:
		if (mode_timer->MessageField == 6)
		{
			WindowScoreOnly(caller);
			return;
		}
		break;
	case 6:
		PlaySound(tabc51wave, "WatchWindowTripwireControl");
		WindowScoreOnly(caller);
		return;
	case 13:
		ScoreValue(caller, 100000);
		return;
	default:
		break;
	}
	PlaySound(tabc04wave, "WatchWindowTripwireControl");
	WindowTail(watch_window_light_group, caller);
}

void GateWindowTripwireControl(MessageCode code, TPinballComponent* caller) // 0x427d10
{
	if (code != MessageCode::ControlCollision)
		return;
	switch (Mode())
	{
	case 2:
		if (v_lite50->MessageField != 0)
			return;
		break;
	case 3:
		if (mode_timer->MessageField == 6)
		{
			WindowScoreOnly(caller);
			return;
		}
		break;
	case 6:
		PlaySound(tabc51wave, "GateWindowTripwireControl");
		WindowScoreOnly(caller);
		return;
	case 13:
		ScoreValue(caller, 100000);
		return;
	default:
		break;
	}
	WindowTail(gate_window_light_group, caller);
}

void Mode13Controller(int code) // 0x42b0e0
{
	if (code == static_cast<int>(MessageCode::ControlMissionComplete))
	{
		ModeStartLights();
		v_lite47->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_dvtraa->Message(MessageCode::TGateDisable, 0.0f);
		a_dvtrab->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrba->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrbb->Message(MessageCode::TGateDisable, 0.0f);
		a_dvtrca->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrcb->Message(MessageCode::TGateDisable, 0.0f);
		v_lite125->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite126->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_dvtrda->Message(MessageCode::TGateDisable, 0.0f);
		a_dvtrdb->Message(MessageCode::TGateEnable, 0.0f);
		prison_window_light_group->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		watch_window_light_group->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		gate_window_light_group->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		control::TableG->MultiballCount += 3;
		a_sink18->Message(MessageCode::TSinkResetTimer, 1.0f);
		v_lite48->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		a_sink17->Message(MessageCode::TSinkResetTimer, 2.0f);
		v_lite49->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 2.0f);
		a_sink16->Message(MessageCode::TSinkResetTimer, 3.0f);
		v_lite50->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 3.0f);
		v_lite78->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		info_text_box->Clear();
	}
	else if (code != static_cast<int>(MessageCode::ControlMissionStarted))
	{
		if (code != static_cast<int>(MessageCode::ControlBallDrained))
			return;
		if (control::TableG->MultiballCount != 1)
			return;
		QuestEndCleanup();
		prison_window_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		watch_window_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		gate_window_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite78->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		SetMode(2);
		MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
		// MissionControl may have started another mode (6 or 13, e.g. a lit tower lock): then the lights stay as it left them.
		if (Mode() != 6 && Mode() != 13)
		{
			v_lite47->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			a_dvtraa->Message(MessageCode::TGateEnable, 0.0f);
			a_dvtrab->Message(MessageCode::TGateDisable, 0.0f);
		}
		if (Mode() == 6 || Mode() == 13)
			return;
		v_lite125->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite126->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		a_dvtrda->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrdb->Message(MessageCode::TGateDisable, 0.0f);
		return;
	}
	ShowText(3051, -1.0f);
}

void Mode14Controller(int code) // 0x42b3c0
{
	if (code == static_cast<int>(MessageCode::ControlMissionComplete))
	{
		ModeStartLights();
		control::TableG->MultiballCount += 1;
		a_sink1->Message(MessageCode::TSinkResetTimer, -1.0f);
		if (S().BallHeld == 0)
			a_kout2->Message(MessageCode::TKickoutRestartTimer, -1.0f);
		v_lite66->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		v_lite65->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		ScoreValue(nullptr, 100000);
		v_lite77->Message(MessageCode::TLightFlasherStartTimedThenStayOn, -1.0f);
		info_text_box->Clear();
		PlaySound(tabc48wave, "Mode14Controller");
		PlayTrack(S().TrackB);
	}
	else if (code != static_cast<int>(MessageCode::ControlMissionStarted))
	{
		if (code != static_cast<int>(MessageCode::ControlBallDrained))
			return;
		if (control::TableG->MultiballCount != 1)
			return;
		QuestEndCleanup();
		v_lite77->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		SetMode(2);
		MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
		v_lite63->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite64->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		return;
	}
	ShowText(3062, -1.0f);
}
}
