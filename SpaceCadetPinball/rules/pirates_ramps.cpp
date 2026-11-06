#include "pch.h"
#include "rules/pirates_rules.h"
#include "table_text.h"
#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TSound.h"
#include "TTextBox.h"
#include <cstdio>
#include <cstdlib>

using namespace pirates;

// Skulduggery rules: the island ramp (a_ramp8) and a_ramp5 combos, the a_ramp13 ramp and the two
// blocker tripwires. Ported from port_listing_pirates.c lines 4035-4180, 4181-4318, 4319-4345, 4346-4361 and 4387-4410
// (FT 0x428b70, 0x428d80, 0x428f70, 0x429000, 0x4290c0 in PIRATES.EXE). Every Message argument the listing lost
// (the combo light group sends at listing lines 4054, 4074, 4126, 4134, 4200, 4212, 4264, 4272) was read from the push sequence
// of the call in the machine code; the receivers and values are repeated in the comments below.
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// crt_sprintf(buf, get_rc_string(id), value) + TTextBox::Display(buf, 2.0, 0, 0) (FT 0x41de40). The format is the EXE's own
// string, so the format-nonliteral warning is off for the one call; the long cast serves %ld and %d alike on x86-64.
void ShowScoreText(int stringId, int value)
{
	const char* format = table_text::Get(stringId);
	if (!format)
		Fatal("ramps: table text id missing from the EXE");
	char text[64]; // the original's buffer is 0x40 bytes
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
	std::snprintf(text, sizeof text, format, static_cast<long>(value));
#pragma GCC diagnostic pop
	info_text_box->Display(text, 2.0f, 0, false);
}

// The lights and strings of one combo ramp (IslandRampControl 0x428b70 / Ramp5Control 0x428d80). The two functions are the
// same code over different parts: the combo group, its first two lights, the wind group and its first two lights.
struct ComboRamp
{
	TLightGroup*& ComboGroup; // left_island_combo_light_group (FT 0x43b394) / right_island_combo_light_group (0x43b4ac)
	TLight*& ComboLight0;     // v_lite629 (0x43b33c) / v_lite626 (0x43b454): flashed when the combo group has no light lit
	TLight*& ComboLight1;     // v_lite630 (0x43b354) / v_lite627 (0x43b46c): flashed with the first when one is lit
	TLightGroup*& WindGroup;  // left_island_light_group (0x43b324) / right_wind_light_group (0x43b43c)
	TLight*& WindLight0;      // v_lite603 (0x43b2d4) / v_lite611 (0x43b3ec)
	TLight*& WindLight1;      // v_lite604 (0x43b2ec) / v_lite612 (0x43b404)
	int StringId;             // 3004 / 3005
};

// 0x4d: another part scored, so this ramp's combo ends. FT: combo group Message(0x14 TLightResetAndTurnOff, 2.0f).
void ComboScorerReplaced(const ComboRamp& r)
{
	r.ComboGroup->Message(MessageCode::TLightResetAndTurnOff, 2.0f);
}

// 0x56: the ball made the ramp.
void ComboBallToLayer2(const ComboRamp& r, TPinballComponent* caller)
{
	if (info_text_box->MessageField != 5)
	{
		tabb12wave->Play(nullptr, "RampControl");
		// Combo step by the lit count of the combo group (Message 0x29 TLightGroupGetOnCount, 0): 0 / 1 / 2 / 3 lit.
		const int combo = r.ComboGroup->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		int index;
		if (combo == 0)
		{
			r.ComboLight0->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			index = 0;
		}
		else if (combo == 1)
		{
			r.ComboLight0->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			r.ComboLight1->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			index = 3;
		}
		else
		{
			// the lost sends: combo group Message(0xf TLightFlasherStartTimedThenStayOn, 2.0f), index 6 for two lit, 9 for more
			r.ComboGroup->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			index = combo == 2 ? 6 : 9;
		}
		// Wind step by the lit count of the wind group; in mode 2 the next wind light(s) flash.
		const int wind = r.WindGroup->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		if (wind == 0)
		{
			if (info_text_box->MessageField == 2)
				r.WindLight0->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		}
		else if (wind == 1)
		{
			if (info_text_box->MessageField == 2)
			{
				r.WindLight0->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
				r.WindLight1->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			}
			index += 1;
		}
		else
		{
			if (info_text_box->MessageField == 2)
				r.WindGroup->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			index += 2;
		}
		// One ScorePart call: Ghidra prints it twice; the printed value is its result.
		ShowScoreText(r.StringId, ScorePart(caller, index));
		return;
	}
	// Mode 5: a flashing wind light means the ramp is the wind target.
	if (r.WindLight0->FlasherOnFlag != 0)
	{
		Mode5WindHit();
		return;
	}
	SpecialAddScore(caller, 50000);
	tabb12wave->Play(nullptr, "RampControl");
}
}

namespace pirates {
// 0x428b70. a_ramp8, ControlRampPlaneLayer (0x54) also lowers the blocker a_blocg.
void IslandRampControl(MessageCode code, TPinballComponent* caller)
{
	const ComboRamp ramp = {left_island_combo_light_group, v_lite629, v_lite630, left_island_light_group, v_lite603, v_lite604, 3004};
	switch (code)
	{
	case MessageCode::ControlScorerReplaced:
		ComboScorerReplaced(ramp);
		break;
	case MessageCode::ControlRampPlaneLayer:
		a_blocg->Message(MessageCode::TBlockerSetFrame, -1.0f);
		break;
	case MessageCode::ControlBallToLayer2:
		ComboBallToLayer2(ramp, caller);
		break;
	default:
		break;
	}
}

// 0x428d80. a_ramp5: the same combo, no plane-layer branch.
void Ramp5Control(MessageCode code, TPinballComponent* caller)
{
	const ComboRamp ramp = {right_island_combo_light_group, v_lite626, v_lite627, right_wind_light_group, v_lite611, v_lite612, 3005};
	switch (code)
	{
	case MessageCode::ControlScorerReplaced:
		ComboScorerReplaced(ramp);
		break;
	case MessageCode::ControlBallToLayer2:
		ComboBallToLayer2(ramp, caller);
		break;
	default:
		break;
	}
}

// 0x428f70. a_ramp13 scores on layer 1 (0x55) and raises the blocker a_blocc on layer 2 (0x56).
void Ramp13Control(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlBallToLayer1)
	{
		ShowScoreText(3006, ScorePart(caller, 0));
		tabb41wave->Play(nullptr, "Ramp13Control");
	}
	else if (code == MessageCode::ControlBallToLayer2)
	{
		a_blocc->Message(MessageCode::TBlockerSetFrame, -1.0f);
	}
}

// 0x429000. s_trip1: blocker a_bloce up, light 504 on, light 505 off.
void BlockerUpTripwireControl(MessageCode code, TPinballComponent*)
{
	if (code != MessageCode::ControlCollision)
		return;
	a_bloce->Message(MessageCode::TBlockerSetFrame, 1.0f);
	v_lite504->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	v_lite505->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x4290c0. s_trip2: in modes 2, 12 and 13 the blocker goes down and the lights swap back; in mode 4 the Ludgg targets reset.
void BlockerDownTripwireControl(MessageCode code, TPinballComponent*)
{
	if (code != MessageCode::ControlCollision)
		return;
	switch (info_text_box->MessageField)
	{
	case 2:
	case 0xc:
	case 0xd:
		a_bloce->Message(MessageCode::TBlockerSetFrame, 0.0f);
		v_lite504->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite505->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		break;
	case 4:
		ResetLduggTargets();
		break;
	default:
		break;
	}
}
}
