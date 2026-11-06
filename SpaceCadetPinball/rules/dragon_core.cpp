#include "pch.h"
#include "rules/dragon_rules.h"
#include "TDiverter.h"
#include "THole.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TSink.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "table_text.h"
#include <cstdio>
#include <cstdlib>

// Dragon's Keep rules: the jackpot and extra-ball awards, the flurry check, the jackpot ramp / ramp 6 / super
// jackpot one-way controls, the mode start / end light helpers and the mode 3 and mode 15
// controllers (port of DRAGON.EXE 0x4231e0, 0x423870, 0x4241c0, 0x424490, 0x4244e0, 0x4265e0, 0x426740, 0x4267f0,
// 0x428580, 0x4285a0, 0x428870, 0x42b510).
namespace dragon {
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// get_rc_string + TTextBox::Display. The text comes from the user's EXE; without it nothing is shown (no substitute).
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

// get_rc_string + crt_sprintf(64-byte buffer, format, value) + TTextBox::Display.
void ShowTextValue(int id, int value, float time)
{
	const char* format = table_text::Get(id);
	if (!format)
	{
		std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), nothing shown\n", id);
		return;
	}
	char buffer[64];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
	std::snprintf(buffer, sizeof buffer, format, value);
#pragma GCC diagnostic pop
	info_text_box->Display(buffer, time, 0, false);
}

const int FlurryLights = 6; // v_lite22 .. v_lite27
}

void table_add_extra_ball(float t) // 0x4231e0
{
	control::TableG->ExtraBalls = 1;
	tabc21wave->Play(nullptr, "table_add_extra_ball");
	if (t >= 0.0f)
		ShowText(3011, t);
}

void AddSuperJackpot(int n) // 0x423870
{
	TPinballTable* table = control::TableG;
	table->SuperJackpotScore += n;
	if (table->SuperJackpotScore > 20000000)
		table->SuperJackpotScore = 20000000;
}

void ReleaseCapturedBall(component_tag_base& sink, float value) // 0x4241c0
{
	if (S().BallHeld != 0)
	{
		s_hole118->MessageField = 2;
		s_hole118->Message(MessageCode::THoleEjectBall, 0.0f);
		return;
	}
	TPinballComponent* component = sink.GetComponent();
	if (!component)
		Fatal("ReleaseCapturedBall: sink not linked");
	component->Message(MessageCode::TSinkResetTimer, value);
}

float AwardJackpot(float value, char showText) // 0x424490
{
	tabc43wave->Play(nullptr, "AwardJackpot");
	ScoreValue(nullptr, control::TableG->JackpotScore);
	if (showText != 0)
		info_text_box->Display("\"", 2.0f, 1, false); // the glyph string at 0x439a5c, font slot 1
	return value;
}

int CheckMode15Start() // 0x4244e0
{
	if (v_lite22->light_on() && v_lite23->light_on() && v_lite24->light_on() && v_lite25->light_on() &&
		v_lite26->light_on() && v_lite27->light_on())
	{
		SetMode(15);
		MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
		return 1;
	}
	return 0;
}

void JackpotRampControl(MessageCode code, TPinballComponent* caller) // 0x4265e0, a_ramp103
{
	if (code != MessageCode::ControlBallToLayer2)
		return;
	if (info_text_box->MessageField != 4)
		tabc18wave->Play(nullptr, "JackpotRampControl");
	if (!v_lite67->light_on())
		caller->MessageField = 0;
	else if (caller->MessageField < 9)
		caller->MessageField = caller->MessageField + 1;
	v_lite67->Message(MessageCode::TLightTurnOn, 0.0f);
	v_lite67->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 20.0f);
	if (info_text_box->MessageField != 6 && info_text_box->MessageField != 13)
	{
		v_lite125->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite126->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		a_dvtrda->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrdb->Message(MessageCode::TGateDisable, 0.0f);
	}
	v_lite119->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	if (info_text_box->MessageField != 4)
	{
		const int score = (caller->MessageField + 1) * 50000;
		ScoreValue(caller, score);
		AddJackpot(score);
	}
	if (info_text_box->MessageField == 2)
	{
		v_lite24->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		if (CheckMode15Start() == 0)
			ShowText(3006, 2.0f);
	}
}

void Ramp6Control(MessageCode code, TPinballComponent*) // 0x426740, a_ramp6
{
	if (code != MessageCode::ControlBallToLayer2)
		return;
	bool awardSpell = true;
	if (info_text_box->MessageField == 2)
	{
		v_lite26->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		if (CheckMode15Start() != 0)
			awardSpell = false;
		else if (!v_lite70->light_on())
		{
			tabc09wave->Play(nullptr, "Ramp6Control");
			awardSpell = false;
		}
	}
	else if (!v_lite70->light_on())
	{
		tabc09wave->Play(nullptr, "Ramp6Control");
		awardSpell = false;
	}
	if (awardSpell)
		AwardRandomSpell();
	v_lite70->Message(MessageCode::TLightTurnOn, 0.0f);
	v_lite70->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 10.0f);
}

void SuperJackpotOneWayControl(MessageCode code, TPinballComponent* caller) // 0x4267f0, s_oneway5
{
	if (code == MessageCode::ControlTimerExpired)
	{
		v_lite68->MessageField = 0;
		return;
	}
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField == 2)
	{
		v_lite125->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite126->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_dvtrda->Message(MessageCode::TGateDisable, 0.0f);
		a_dvtrdb->Message(MessageCode::TGateEnable, 0.0f);
	}
	else if (info_text_box->MessageField == 4)
		return;
	tabc20wave->Play(nullptr, "SuperJackpotOneWayControl");
	ScorePart(caller, v_lite68->MessageField);
	if (v_lite68->MessageField < 4)
		v_lite68->MessageField = v_lite68->MessageField + 1;
	AddSuperJackpot(200000);
	ShowTextValue(3076, 200000, 2.0f);
	v_lite68->Message(MessageCode::TLightTurnOn, 0.0f);
	v_lite68->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 10.0f);
}

void ModeStartLights() // 0x428580
{
	ResetTowerLocks();
	dragon_letter_light_group->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
}

void QuestEndCleanup() // 0x4285a0
{
	info_text_box->Clear();
	v_lite63->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite64->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	dragon_letter_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
	UpdateTowerLocks();
}

void Mode3Controller(int code, TPinballComponent* caller) // 0x428870
{
	const int modeEnd = static_cast<int>(MessageCode::ControlModeEnd); // FT 0x52
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		if (caller == mode_timer)
		{
			v_lite63->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			v_lite64->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			MissionControlSwitch(mode_timer->MessageField, modeEnd, nullptr);
			mode_timer->MessageField = -1;
			if (info_text_box->MessageField == 3)
			{
				SetMode(2);
				MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
			}
		}
		return;
	case MessageCode::ControlMissionComplete:
		v_lite63->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite64->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		mode_timer->Message(MessageCode::TTimerResetTimer, 10.0f);
		// falls through to the countdown text
	case MessageCode::ControlMissionStarted:
	{
		const int timeLeft = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
		if (timeLeft != 0)
			ShowTextValue(3067, timeLeft / 1000, 0.5f);
		return;
	}
	case MessageCode::ControlBallDrained:
		MissionControlSwitch(mode_timer->MessageField, modeEnd, nullptr);
		mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
		mode_timer->MessageField = -1;
		SetMode(2);
		return;
	default:
		return;
	}
}

void Mode15Controller(int code) // 0x42b510
{
	TPinballTable* table = control::TableG;
	if (code == static_cast<int>(MessageCode::ControlMissionComplete))
	{
		ModeStartLights();
		table->MultiballCount += 4;
		if (S().BallHeld != 0)
		{
			table->MultiballCount -= 1;
			s_plunger1->Message(MessageCode::PlungerRelaunchBall, 0.96f);
		}
		a_sink1->Message(MessageCode::TSinkResetTimer, -1.0f);
		v_lite66->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		a_sink2->Message(MessageCode::TSinkResetTimer, -1.0f);
		v_lite65->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		a_sink4->Message(MessageCode::TSinkResetTimer, 1.0f);
		well_light_group->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		a_sink18->Message(MessageCode::TSinkResetTimer, 1.0f);
		v_lite48->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		dragon_letter_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
		TLight* const flurry[FlurryLights] = {v_lite22, v_lite23, v_lite24, v_lite25, v_lite26, v_lite27};
		for (TLight* light : flurry)
			light->Message(MessageCode::TLightTurnOn, 0.0f);
		for (TLight* light : flurry)
			light->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite79->Message(MessageCode::TLightFlasherStartTimedThenStayOn, -1.0f);
		info_text_box->Clear();
		tabc61wave->Play(nullptr, "Mode15Controller");
	}
	else if (code != static_cast<int>(MessageCode::ControlMissionStarted))
	{
		if (code != static_cast<int>(MessageCode::ControlBallDrained) || table->MultiballCount != 1)
			return;
		QuestEndCleanup();
		v_lite79->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		TLight* const flurry[FlurryLights] = {v_lite22, v_lite23, v_lite24, v_lite25, v_lite26, v_lite27};
		for (TLight* light : flurry)
			light->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		SetMode(2);
		MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
		return;
	}
	ShowText(3063, -1.0f);
}
}
