#include "pch.h"
#include "rules/pirates_rules.h"
#include "table_text.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TSink.h"
#include "TSound.h"
#include "TTextBox.h"
#include <cstdio>
#include <cstdlib>

using namespace pirates;

// Skulduggery rules: the two orbit sinks and the two orbit tripwires. Ported from
// port_listing_pirates.c lines 3837-3877 (OrbitSinkControl1), 3878-3918 (OrbitSinkControl2), 4446-4583
// (LeftOrbitTripwireControl) and 4584-4730 (RightOrbitTripwireControl); FT 0x428700, 0x428800, 0x429210, 0x429400
// (PIRATES.EXE). Every Message argument the listing lost was read from the push sequence of the call
// in the disassembly; the values are named in the comments below.
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// crt_sprintf(buf, get_rc_string(id), value) + TTextBox::Display(buf, 2.0, 0, 0) (FT 0x41de40: text, time, font, low priority).
// The long cast satisfies %ld and %d alike on x86-64.
void ShowScoreText(int stringId, int value)
{
	const char* format = table_text::Get(stringId);
	if (!format)
		Fatal("orbits: table text id missing from the EXE");
	char buf[64];
	std::snprintf(buf, sizeof buf, format, static_cast<long>(value));
	info_text_box->Display(buf, 2.0f, 0, false);
}

// One orbit sink (OrbitSinkControl1 / 2): the two differ only in their treasure light, their light group and the other sink.
void OrbitSinkBody(TLight* treasureLight, TLightGroup* orbitLights, TSink* otherSink, MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	const int mode = info_text_box->MessageField;
	if (mode == 8)
	{
		if (treasureLight->FlasherOnFlag == 0)
		{
			// ScorePart is called once; the printed value is its result (listing: ret_of).
			ShowScoreText(3002, ScorePart(caller, 0));
			tabb11wave->Play(nullptr, "OrbitSinkControl");
			HideawayExit();
			return;
		}
		treasureLight->Message(MessageCode::TLightResetAndTurnOn, 0.0f);   // push 0x13, value = the push 0 hoisted at 0x428746
		orbitLights->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		AwardNextSpecialTreasure(0.0f);
		LightNextTreasureX();
		ReleaseHoldSink(-1.0f);
		return;
	}
	if (mode != 0xb)
	{
		caller->ActiveFlag = 0;
		otherSink->Message(MessageCode::TSinkResetTimer, 0.0f);   // push 0x41, push 0 at 0x428724
		return;
	}
	AwardNextSpecialTreasure(0.0f);
	HideawayExit();
}

// One orbit tripwire (LeftOrbitTripwireControl s_trip3 / RightOrbitTripwireControl s_trip8). The two are the same function with
// their own lights; the right one runs the hideaway step first when the mode is 2.
struct OrbitWire
{
	TLightGroup* Combo;      // left / right_orbit_combo_light_group: the combo steps (0 / 3 / 6 / 9)
	TLight* ComboLightA;     // v_lite631 / v_lite634: flashed at combo steps 0 and 1
	TLight* ComboLightB;     // v_lite632 / v_lite635: flashed together with A at combo step 1
	TLightGroup* Orbit;      // left / right_orbit_light_group: adds 0 / 1 / 2 in mode 2
	TLight* OrbitLightA;     // v_lite600 / v_lite608: mode 5 test, flashed at orbit count 0 and 1
	TLight* OrbitLightB;     // v_lite601 / v_lite609: flashed together with A at orbit count 1
};

void OrbitWireBody(const OrbitWire& w, MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField == 5)
	{
		if (w.OrbitLightA->FlasherOnFlag != 0)
		{
			Mode5WindHit();
			return;
		}
		SpecialAddScore(caller, 50000);   // push 0xc350
		tabb12wave->Play(nullptr, "OrbitTripwireControl");
		return;
	}
	tabb12wave->Play(nullptr, "OrbitTripwireControl");
	int index;
	const int combo = w.Combo->Message(MessageCode::TLightGroupGetOnCount, 0.0f);   // push 0x29, push 0
	if (combo == 0)
	{
		w.ComboLightA->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);   // push 2.0, push 0xf
		index = 0;
	}
	else if (combo == 1)
	{
		w.ComboLightA->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		w.ComboLightB->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 3;
	}
	else
	{
		w.Combo->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = (combo == 2) ? 6 : 9;
	}
	const int orbit = w.Orbit->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	if (orbit == 0)
	{
		if (info_text_box->MessageField == 2)
			w.OrbitLightA->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
	}
	else if (orbit == 1)
	{
		if (info_text_box->MessageField == 2)
		{
			w.OrbitLightA->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			w.OrbitLightB->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		}
		index += 1;
	}
	else
	{
		if (info_text_box->MessageField == 2)
			w.Orbit->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index += 2;
	}
	ShowScoreText(3008, ScorePart(caller, index));
}
} // namespace

namespace pirates {
// FT 0x428700 (a_sink9)
void OrbitSinkControl1(MessageCode code, TPinballComponent* caller)
{
	OrbitSinkBody(v_mlite110, left_orbit_light_group, a_sink12, code, caller);
}

// FT 0x428800 (a_sink12)
void OrbitSinkControl2(MessageCode code, TPinballComponent* caller)
{
	OrbitSinkBody(v_mlite104, right_orbit_light_group, a_sink9, code, caller);
}

// FT 0x429210 (s_trip3). 0x4d (ControlScorerReplaced): left combo group Message(0x14, 2.0) (push 2.0, push 0x14 at 0x4293e5).
void LeftOrbitTripwireControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlScorerReplaced)
	{
		left_orbit_combo_light_group->Message(MessageCode::TLightResetAndTurnOff, 2.0f);
		return;
	}
	const OrbitWire wire = {left_orbit_combo_light_group, v_lite631, v_lite632, left_orbit_light_group, v_lite600, v_lite601};
	OrbitWireBody(wire, code, caller);
}

// FT 0x429400 (s_trip8). Mode 2 first lights the hideaway step: v_lite527 flasher (1.0), bit 8 of v_lite524, CheckHideawayComplete(0)
// (0x429483-0x4294a7), then the common body.
void RightOrbitTripwireControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlScorerReplaced)
	{
		right_orbit_combo_light_group->Message(MessageCode::TLightResetAndTurnOff, 2.0f);   // 0x42960f
		return;
	}
	if (code == MessageCode::ControlCollision && info_text_box->MessageField == 2)
	{
		v_lite527->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite524->MessageField = v_lite524->MessageField | 8;
		CheckHideawayComplete(0.0f);
	}
	const OrbitWire wire = {right_orbit_combo_light_group, v_lite634, v_lite635, right_orbit_light_group, v_lite608, v_lite609};
	OrbitWireBody(wire, code, caller);
}
} // namespace pirates
