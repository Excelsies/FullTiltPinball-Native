#include "pch.h"
#include "rules/dragon_rules.h"
#include "TBlocker.h"
#include "TBumper.h"
#include "TDiverter.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TLightSweeps.h"
#include "TPinballTable.h"
#include "TRollover.h"
#include "TSink.h"
#include "TSoloTarget.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "TTripwire.h"
#include "table_text.h"
#include <cstdio>
#include <cstdlib>

// Dragon's Keep rules: the playfield parts with a small control function and mode 7. Port of DRAGON.EXE
// PassRockAControl 0x423810, PassRockBControl 0x423840, BumperControl 0x424bc0, PassTargetBank 0x4252b0, PassTargetControl1-4
// 0x4254a0-0x4255e0, LightTargetControl1/2 0x4258b0/0x4258e0, RidgeControl 0x4269a0, RolloverScoreControl1/2 0x426ac0/0x426ae0 and
// Mode7Controller 0x429b10 (port_listing_dragon.c lines 644-671, 1364-1412, 1711-1851, 1984-2019, 2684-2727, 2748-2775, 4898-4994).
// The Message arguments Ghidra lost and the receivers it shows as unaff_retaddr (= the calling part) were read
// from the machine code. The one rule global read is the mode 7 music slot 0x439af0 (DragonState::TrackMode7).
namespace dragon {
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// TTextBox::Display(info_text_box, get_rc_string(id), time, 0, 0). The text comes from the user's EXE at run time; when it is
// unavailable nothing is shown (the reader has logged why), never a substitute text.
void ShowText(int id, float time)
{
	const char* text = table_text::Get(id);
	if (!text)
	{
		std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), nothing shown\n", id);
		return;
	}
	info_text_box->Display(text, time, 0, false);
}

// get_rc_string(id) + crt_sprintf(64-byte buffer, format, value) + TTextBox::Display(buffer, time, 0, 0).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
void ShowTextValue(int id, int value, float time)
{
	const char* format = table_text::Get(id);
	if (!format)
	{
		std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), nothing shown\n", id);
		return;
	}
	char buffer[64];
	std::snprintf(buffer, sizeof buffer, format, static_cast<long>(value));
	info_text_box->Display(buffer, time, 0, false);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// 0x4252b0: one pass target was hit. The bank is complete when the four lamps v_lite14/13/12/11 are all lit (light_on(): on, flashing or
// toggled); then the lamps flash once more and go dark, the diverters and rocks reset, text 3004 (unless mode 15 starts) and 15,000.
void PassTargetBank(TPinballComponent* hit)
{
	if (!v_lite14->light_on() || !v_lite13->light_on() || !v_lite12->light_on() || !v_lite11->light_on())
	{
		if (info_text_box->MessageField == 4)
			return;
		tabc08wave->Play(nullptr, "PassTargetBank");
		ScorePart(hit, 0);
		return;
	}
	v_lite14->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	v_lite13->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	v_lite12->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	v_lite11->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	a_dvtrba->Message(MessageCode::TGateDisable, 0.0f);
	a_dvtrbb->Message(MessageCode::TGateEnable, 0.0f);
	a_dvtrca->Message(MessageCode::TGateDisable, 0.0f);
	a_dvtrcb->Message(MessageCode::TGateEnable, 0.0f);
	v_lite56->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	v_lite55->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	v_lite46->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_rockb->Message(MessageCode::TBlockerEnable, 0.0f);
	v_lite45->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_rocka->Message(MessageCode::TBlockerEnable, 0.0f);
	bool showText = true;
	if (info_text_box->MessageField == 2)
	{
		v_lite25->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		if (CheckMode15Start() != 0)
			showText = false;
	}
	if (showText)
		ShowText(3004, 2.0f);
	if (info_text_box->MessageField != 4)
	{
		tabc08wave->Play(nullptr, "PassTargetBank");
		ScorePart(hit, 1);
	}
}

// Mode 7 music (slot 0x439af0).
void PlayPassTrack() { PlayTrack(S().TrackMode7); }

// Mode 7 pieces, one per control code (FT jump table 0x429e8c, codes 0x46..0x53).
void ModeComplete()
{
	light_sweeps->Message(MessageCode::TLightSweepsStart, 3.0f);
	gargoyle_spot_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	if (info_text_box->MessageField == 2)
		v_lite46->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	a_rockb->Message(MessageCode::TBlockerDisable, 0.0f);
	a_spot14->Message(MessageCode::TSoloTargetDisable, 0.0f);
	v_lite45->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	a_rocka->Message(MessageCode::TBlockerDisable, 0.0f);
	a_spot10->Message(MessageCode::TSoloTargetDisable, 0.0f);
	ScoreValue(nullptr, 100000);
	info_text_box->Clear();
	ShowText(3036, -1.0f);
	v_lite46->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	v_lite45->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	tabc53wave->Play(nullptr, "Mode7Controller");
	PlayPassTrack();
}

// ControlMissionStarted (also the tail of ControlModeResume): the countdown text while the mode timer runs.
void ShowCountdown()
{
	const int timeLeft = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
	if (static_cast<double>(timeLeft) != 0.0)
		ShowTextValue(3035, timeLeft / 1000, 0.5f);
}

void ModeResume()
{
	v_lite46->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	v_lite45->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	tabc53wave->Play(nullptr, "Mode7Controller");
	gargoyle_spot_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
	mode_timer->Message(MessageCode::TTimerResetTimer, 15.0f);
	if (info_text_box->HasPendingMessage())
		return;
	ShowCountdown();
}

// ControlModeEnd, and the tail of ControlBallDrained.
void ModeEnd()
{
	QuestEndCleanup();
	gargoyle_spot_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	gargoyle_spot_light_group->Message(MessageCode::TLightGroupReset, 0.0f);
	gargoyle_spot_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
	v_lite46->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_rockb->Message(MessageCode::TBlockerEnable, 0.0f);
	v_lite45->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_rocka->Message(MessageCode::TBlockerEnable, 0.0f);
	CompleteQuest();
}
}

void PassRockAControl(MessageCode code, TPinballComponent* caller)   // 0x423810, a_rocka
{
	if (code == MessageCode::ControlBlockerMoveDone && static_cast<TBlocker*>(caller)->FtCurrentState() != -1)
		a_spot10->Message(MessageCode::TSoloTargetEnable, 0.0f);
}

void PassRockBControl(MessageCode code, TPinballComponent* caller)   // 0x423840, a_rockb
{
	if (code == MessageCode::ControlBlockerMoveDone && static_cast<TBlocker*>(caller)->FtCurrentState() != -1)
		a_spot14->Message(MessageCode::TSoloTargetEnable, 0.0f);
}

void BumperControl(MessageCode code, TPinballComponent* caller)   // 0x424bc0, a_bump1..4
{
	if (code != MessageCode::ControlCollision)
		return;
	const bool hoard = info_text_box->MessageField == 4;   // mode 4: no sound, no score
	if (!hoard)
		tabc03wave->Play(nullptr, "BumperControl");
	// a_bump3 / a_bump4 flash their lamp (code 7 = TLightFlasherStartTimed, 0.5 s; read from 0x424bf1 / 0x424c0f)
	if (caller == a_bump3)
		v_lite65->Message(MessageCode::TLightFlasherStartTimed, 0.5f);
	else if (caller == a_bump4)
		v_lite66->Message(MessageCode::TLightFlasherStartTimed, 0.5f);
	if (!hoard)
		ScorePart(caller, 0);
	// a_bump1 holds the contact counter of all four bumpers
	const int hits = a_bump1->MessageField + 1;
	if (hits == 10)
	{
		a_bump1->MessageField = 0;
		AwardNextWeapon();
		return;
	}
	a_bump1->MessageField = hits;
}

void PassTargetControl1(MessageCode code, TPinballComponent* caller)   // 0x4254a0, a_spot10
{
	if (code != MessageCode::ControlCollision)
		return;
	v_lite45->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	a_rocka->Message(MessageCode::TBlockerDisable, 0.0f);
	a_spot10->Message(MessageCode::TSoloTargetDisable, 0.0f);
	v_lite14->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	PassTargetBank(caller);
}

void PassTargetControl2(MessageCode code, TPinballComponent* caller)   // 0x425500, a_spot8
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField != 7)
	{
		v_lite13->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		PassTargetBank(caller);
		return;
	}
	v_lite13->Message(MessageCode::TLightFlasherStartTimed, 0.5f);
	AddJackpot(250000);
	ScoreValue(caller, 100000);
}

void PassTargetControl3(MessageCode code, TPinballComponent* caller)   // 0x425570, a_spot14
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField == 2)
		v_lite46->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	a_rockb->Message(MessageCode::TBlockerDisable, 0.0f);
	a_spot14->Message(MessageCode::TSoloTargetDisable, 0.0f);
	v_lite12->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	PassTargetBank(caller);
}

void PassTargetControl4(MessageCode code, TPinballComponent* caller)   // 0x4255e0, a_spot12
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField != 7)
	{
		v_lite11->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		PassTargetBank(caller);
		return;
	}
	v_lite11->Message(MessageCode::TLightFlasherStartTimed, 0.5f);
	AddJackpot(250000);
	ScoreValue(caller, 100000);
}

void LightTargetControl1(MessageCode code, TPinballComponent* caller)   // 0x4258b0, a_spot30
{
	if (code != MessageCode::ControlCollision)
		return;
	v_lite20->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	ScorePart(caller, 0);
}

void LightTargetControl2(MessageCode code, TPinballComponent* caller)   // 0x4258e0, a_spot31
{
	if (code != MessageCode::ControlCollision)
		return;
	v_lite19->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	ScorePart(caller, 0);
}

void RidgeControl(MessageCode code, TPinballComponent* caller)   // 0x4269a0, s_trip1 / s_trip2 / v_lite69
{
	if (code == MessageCode::ControlTimerExpired)
	{
		v_lite69->MessageField = 0;
		return;
	}
	if (code != MessageCode::ControlCollision)
		return;
	// one ScorePart call: its result is the value of text 3007 (0x4269d7 call 0x4230a0, push eax before the sprintf)
	const int added = R().ScorePart(caller, v_lite69->MessageField);
	char buffer[64];
	const char* format = table_text::Get(3007);
	if (format)
	{
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
		std::snprintf(buffer, sizeof buffer, format, static_cast<long>(added));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	}
	else
		std::fprintf(stderr, "FT rules: text 3007 unavailable (EXE missing or string absent), nothing shown\n");
	if (v_lite69->MessageField < 3)
		v_lite69->MessageField = v_lite69->MessageField + 1;
	v_lite69->Message(MessageCode::TLightTurnOn, 0.0f);                                   // 0x426a0b-0x426a1c: code 1, value 0
	v_lite69->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 20.0f);            // 0x426a1f-0x426a2e: code 0x10, value 0x41a00000
	if (format)
		info_text_box->Display(buffer, 2.0f, 0, false);
}

void RolloverScoreControl1(MessageCode code, TPinballComponent* caller)   // 0x426ac0, a_roll5
{
	if (code == MessageCode::ControlCollision)
		ScorePart(caller, 0);
}

void RolloverScoreControl2(MessageCode code, TPinballComponent* caller)   // 0x426ae0, a_roll2
{
	if (code == MessageCode::ControlCollision)
		ScorePart(caller, 0);
}

void Mode7Controller(int code, TPinballComponent* caller)   // 0x429b10
{
	bool started = false;   // run the ControlMissionStarted countdown
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		if (!caller)
			Fatal("mode 7: timer expiry without a component");
		if (caller->TypeId == 1032)   // TTimer
		{
			if (caller != mode_timer)
				return;
			v_lite46->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			v_lite45->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			gargoyle_spot_light_group->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
			mode_timer->MessageField = info_text_box->MessageField;
			info_text_box->MessageField = 3;
			MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
			return;
		}
		if (caller->TypeId != 1038)   // TLightSweeps: the intro sweep ended
			return;
		ModeStartLights();
		v_lite14->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		gargoyle_spot_light_group->Message(MessageCode::TLightGroupAnimationBackward, 0.1f);
		ReleaseCapturedBall(a_sink18_tag, 0.0f);
		mode_timer->Message(MessageCode::TTimerResetTimer, 30.65f);
		MissionControl(static_cast<int>(MessageCode::ControlMissionStarted), nullptr);
		return;
	case MessageCode::ControlMissionComplete:
		ModeComplete();
		return;
	case MessageCode::ControlModeResume:
		ModeResume();
		return;
	case MessageCode::ControlMissionStarted:
		started = true;
		break;
	case MessageCode::ControlBallDrained:
		info_text_box->MessageField = 2;
		mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
		ModeEnd();
		return;
	case MessageCode::ControlModeEnd:
		ModeEnd();
		return;
	default:
		return;
	}
	if (started)
		ShowCountdown();
}
}
