#include "pch.h"
#include "rules/pirates_rules.h"
#include "table_text.h"
#include "TKickback.h"
#include "TLight.h"
#include "TOneway.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TSoloTarget.h"
#include "TTextBox.h"
#include <cstdio>
#include <cstdlib>

// Skulduggery rules: the lava bank (a_spot8..a_spot11), the lava lights, mode 12 and
// CheckSkulduggeryLava. Ported from port_listing_pirates.c lines 618-630, 1787-1846, 2297-2314, 3175-3328 and 7102-7150
// (FT 0x423610, 0x425310, 0x4253a0, 0x425e30, 0x427540, 0x427580-0x4277f0, 0x42d480); the control functions, the bank and the
// lights were re-read in machine code (capstone, PIRATES.EXE).
namespace pirates {
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

enum { ModeIdle = 2, Mode10Num = 10 }; // info_text_box->MessageField values tested by the lava targets

// The bank order of the original (a_spot11 bits 1, 2, 4, 8 = v_lite551, 550, 549, 548).
const int LavaBit[4] = {1, 2, 4, 8};
}

// 0x423610 StartMode12
void StartMode12()
{
	ModeRunningLightOn();
	SetMode(12);
	MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
}

// 0x425310 SetLavaLights: param 0 -> the four lights reset and off (0x14, 0.0); any other -> flasher timed then off (0x10, 1.0).
// Only param 1 clears the bank bits.
void SetLavaLights(int param)
{
	const float value = param != 0 ? 1.0f : 0.0f;
	const MessageCode code = param == 0 ? MessageCode::TLightResetAndTurnOff : MessageCode::TLightFlasherStartTimedThenStayOff;
	if (param == 1)
		a_spot11->MessageField = 0;
	v_lite551->Message(code, value);
	v_lite550->Message(code, value);
	v_lite549->Message(code, value);
	v_lite548->Message(code, value);
}

// 0x4253a0 RestoreLavaLights: each light follows its bit of the bank state in a_spot11->MessageField; value 0 for all four.
void RestoreLavaLights()
{
	TLight* const lights[4] = {v_lite551, v_lite550, v_lite549, v_lite548};
	for (int i = 0; i < 4; ++i)
	{
		const MessageCode code = (a_spot11->MessageField & LavaBit[i]) != 0 ? MessageCode::TLightResetAndTurnOn
		                                                                    : MessageCode::TLightResetAndTurnOff;
		lights[i]->Message(code, 0.0f);
	}
}

// 0x425e30 CheckSkulduggeryLava: all eleven Skulduggery letters (v_lite1 bits 0..10) -> lights, the three target banks reset,
// a map piece. Returns 1 when it fired.
int CheckSkulduggeryLava()
{
	if ((v_lite1->MessageField & 0x7ff) != 0x7ff)
		return 0;
	SetSkulduggeryLights(1);
	ResetSkuTargets();
	ResetLduggTargets();
	ResetEryTargets();
	AwardMapPiece(0.0f);
	return 1;
}

// 0x427540 LavaTargetBank (param = the target that was hit, the original's caller argument)
void LavaTargetBank(TPinballComponent* caller)
{
	if ((a_spot11->MessageField & 0xf) == 0xf)
	{
		a_spot11->MessageField = 0;
		ScorePart(caller, 1);
		StartMode12();
		return;
	}
	ScorePart(caller, 0);
}

namespace {
// The four target controls differ only in the bank bit, the light and where the sound is played (control 2 plays the idle-mode
// sound before its bank update, 1/3/4 after the bank). Mode 2 fills the bank; mode 10 sets the bit in the shared counter
// 0x435cf8 and runs the mode 10 bank checks; any other mode only scores 1,000 (ScorePart index 0).
void LavaTarget(int i, TLight* light, MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	const int mode = info_text_box->MessageField;
	if (mode == ModeIdle)
	{
		if (i == 1)
			tabb23wave->Play(nullptr, "LavaTargetControl2");
		a_spot11->MessageField |= LavaBit[i];
		light->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		LavaTargetBank(caller);
		if (i != 1)
			tabb23wave->Play(nullptr, i == 0 ? "LavaTargetControl1" : i == 2 ? "LavaTargetControl3" : "LavaTargetControl4");
		return;
	}
	if (mode != Mode10Num)
	{
		tabb23wave->Play(nullptr, "LavaTargetControl");
		ScorePart(caller, 0);
		return;
	}
	S().ModeCounter |= LavaBit[i];
	light->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	if (i == 3)
		a_spot8->MessageField |= 8; // the original's extra write in control 4 (0x427896), reproduced
	CheckMode10Banks(CheckLavaBankMode10(0.0f));
	tabb94wave->Play(nullptr, "LavaTargetControl");
}
}

void LavaTargetControl1(MessageCode code, TPinballComponent* caller) { LavaTarget(0, v_lite551, code, caller); } // a_spot11, 0x427580
void LavaTargetControl2(MessageCode code, TPinballComponent* caller) { LavaTarget(1, v_lite550, code, caller); } // a_spot8,  0x427650
void LavaTargetControl3(MessageCode code, TPinballComponent* caller) { LavaTarget(2, v_lite549, code, caller); } // a_spot9,  0x427720
void LavaTargetControl4(MessageCode code, TPinballComponent* caller) { LavaTarget(3, v_lite548, code, caller); } // a_spot10, 0x4277f0

// 0x42d480 Mode12Controller. Entry (MissionComplete) adds two balls; the drain of the last extra ball
// (MultiballCount 1) ends it.
void Mode12Controller(int code, TPinballComponent*)
{
	if (code == static_cast<int>(MessageCode::ControlMissionComplete))
	{
		ModeStartCleanup();
		a_kick1->Message(MessageCode::FtPartEnable, 0.0f);
		s_oneway12->ActiveFlag = 0;
		if (S().LockedBalls == 0)
			ReleaseHoldSink(2.0f);
		if (S().LockedBalls == 0 || S().LockedBalls == 1)
			ReleaseHoldSink(1.0f);
		control::TableG->MultiballCount += 2;
		S().LockedBalls = 0;
		info_text_box->Clear();
		tabb32wave->Play(nullptr, "Mode12Controller");
	}
	else if (code == static_cast<int>(MessageCode::ControlBallDrained))
	{
		if (control::TableG->MultiballCount != 1)
			return;
		ModeEndRestoreLights();
		info_text_box->Clear();
		a_kick1->Message(MessageCode::FtPartDisable, 0.0f);
		SetMode(2);
		SetLavaLights(1);
		MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
		return;
	}
	else if (code != static_cast<int>(MessageCode::ControlMissionStarted))
		return;
	const char* text = table_text::Get(3055);
	if (!text)
		Fatal("Mode12Controller: string 3055 unavailable");
	info_text_box->Display(text, -1.0f, 0, false);
}
}
