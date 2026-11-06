#include "pch.h"
#include "rules/pirates_rules.h"
#include "table_text.h"
#include <cstdio>
#include <cstdlib>
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballComponent.h"
#include "TSoloTarget.h"
#include "TTextBox.h"
#include "TTimer.h"

// Skulduggery rules: next-mode selection, the mode lights and the eight Start<Mode> functions.
// Ported from port_listing_pirates.c (FT 0x423480..0x4235f0, 0x4255a0..0x425a50, 0x425de0, 0x4273b0). Listing artefacts
// in this range: ModeSelectTargetControl's `unaff_retaddr` is param_2, the caller (the control
// function's second argument). SelectNextMode's two group messages 0x29 / 0x2a are TLightGroupGetOnCount / GetLightCount
// (msgmap, same codes as upstream's 37 / 38).
using namespace pirates;

namespace pirates {
namespace {
// Glyph strings of the original's .data (code constants, not table text): StartMode4 0x43ab08, StartMode5 0x43ab10,
// StartMode7 0x43aaa0, StartMode8 0x43ab0c.
const char AttackGlyph[] = "H";
const char Mode5Glyph[] = "J";
const char Mode7Glyph[] = "333";
const char TreasureGlyph[] = "I";

// Table 0x43d0f0: selection index 0..7 -> mode 4, 8, 7, 5, 6, 9, 10, 11.
const int IndexToMode[8] = {4, 8, 7, 5, 6, 9, 10, 11};

// The mode lights in selection-index order (v_lite552..v_lite559). Index 7 (mode 11) is never a candidate of SelectNextMode.
TLight* ModeLight(int index)
{
	switch (index)
	{
	case 0: return v_lite552;
	case 1: return v_lite553;
	case 2: return v_lite554;
	case 3: return v_lite555;
	case 4: return v_lite556;
	case 5: return v_lite557;
	case 6: return v_lite558;
	default: return v_lite559;
	}
}

// The original's light of a mode (SetModeLight's switch) for modes 4..11; nullptr for every
// other mode.
TLight* LightOfMode(int mode)
{
	switch (mode)
	{
	case 4: return v_lite552;
	case 5: return v_lite555;
	case 6: return v_lite556;
	case 7: return v_lite554;
	case 8: return v_lite553;
	case 9: return v_lite557;
	case 10: return v_lite558;
	case 11: return v_lite559;
	default: return nullptr;
	}
}

// Every Start<Mode>: ModeRunningLightOn, the optional glyph, the mode, MissionControl(ControlMissionComplete, null).
void StartMode(int mode)
{
	info_text_box->MessageField = mode;
	MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
}
}

// 0x423480 StartMode4
void StartMode4()
{
	ModeRunningLightOn();
	info_text_box->Display(AttackGlyph, 1.0f, 1, false);
	StartMode(4);
}

// 0x4234c0 StartMode5
void StartMode5()
{
	ModeRunningLightOn();
	info_text_box->Display(Mode5Glyph, 2.0f, 1, false);
	StartMode(5);
}

// 0x423500 StartMode6
void StartMode6()
{
	ModeRunningLightOn();
	StartMode(6);
}

// 0x423520 StartMode7
void StartMode7()
{
	ModeRunningLightOn();
	info_text_box->Display(Mode7Glyph, 2.0f, 1, false);
	StartMode(7);
}

// 0x423560 StartMode8
void StartMode8()
{
	ModeRunningLightOn();
	info_text_box->Display(TreasureGlyph, 2.0f, 1, false);
	StartMode(8);
}

// 0x4235a0 StartMode9
void StartMode9()
{
	ModeRunningLightOn();
	StartMode(9);
}

// 0x4235c0 StartMode10: a_spot8's MessageField is cleared before the running light (the original's order).
void StartMode10()
{
	a_spot8->MessageField = 0;
	ModeRunningLightOn();
	StartMode(10);
}

// 0x4235f0 StartMode11
void StartMode11()
{
	ModeRunningLightOn();
	StartMode(11);
}

// 0x4255a0 SetModeLight. `on` is a byte in the original (cmp byte [esp+8], 1; sbb): 0 selects the steady light (code 0x13
// TLightResetAndTurnOn), anything else the flasher (code 7 TLightFlasherStartTimed); the value is -1.0 (no timeout) in both.
void SetModeLight(int mode, int on)
{
	ClearSelectedModeLight();
	if (mode == 2)
	{
		SelectNextMode();
		return;
	}
	TLight* light = LightOfMode(mode);
	if (!light)
		return;
	const MessageCode code = static_cast<unsigned char>(on) == 0 ? MessageCode::TLightResetAndTurnOn : MessageCode::TLightFlasherStartTimed;
	light->Message(code, -1.0f);
}

// 0x4256d0 SelectModeIndex: the signed `index < 8` test is the original's (jge).
void SelectModeIndex(int index)
{
	if (index < 8)
	{
		SetModeLight(IndexToMode[index], 1);
		mode_timer->MessageField = index + 1;
	}
}

// 0x425700 ClearSelectedModeLight: the selected index (mode_timer's MessageField, 1..8) loses its light; always cleared to 0.
void ClearSelectedModeLight()
{
	const int selected = mode_timer->MessageField;
	if (selected >= 1 && selected <= 8)
		ModeLight(selected - 1)->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	mode_timer->MessageField = 0;
}

// 0x425850 SelectNextMode: index 7 (mode 11) when every map piece is lit; else a random one of the unlit lights v_lite552..558
// (one draw, index (r*n)>>15, listing order); with all seven lit, the one that is flashing, else mode 11. The flashing
// branch returns without clearing or selecting anything.
int SelectNextMode()
{
	int index;
	if (map_piece_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f)
		== map_piece_light_group->Message(MessageCode::TLightGroupGetLightCount, 0.0f))
	{
		index = 7;
	}
	else
	{
		int candidates[7];
		int count = 0;
		for (int i = 0; i < 7; ++i)
			if (!ModeLight(i)->light_on())
				candidates[count++] = i;
		if (count == 0)
		{
			for (int i = 0; i < 8; ++i)
				if (ModeLight(i)->FlasherOnFlag)
					return i;
			index = 7;
		}
		else
		{
			index = candidates[ft::RandIndex(count)];
		}
	}
	ClearSelectedModeLight();
	SelectModeIndex(index);
	return index;
}

// 0x425a50 StartSelectedMode: selected index 1..8 (mode_timer's MessageField) -> its Start function.
void StartSelectedMode()
{
	switch (mode_timer->MessageField)
	{
	case 1: StartMode4(); return;
	case 2: StartMode8(); return;
	case 3: StartMode7(); return;
	case 4: StartMode5(); return;
	case 5: StartMode6(); return;
	case 6: StartMode9(); return;
	case 7: StartMode10(); return;
	case 8: StartMode11(); return;
	default: return;
	}
}

// 0x425de0 CheckSkulduggeryStart: all 11 SKULDUGGERY letters (the low 11 bits of v_lite1's MessageField) start the selected mode.
int CheckSkulduggeryStart()
{
	if ((v_lite1->MessageField & 0x7ff) != 0x7ff)
		return 0;
	S().NoHeldBall = 1;
	SetSkulduggeryLights(1);
	ResetSkuTargets();
	ResetLduggTargets();
	ResetEryTargets();
	StartSelectedMode();
	return 1;
}

// 0x4273b0 ModeSelectTargetControl (a_spot102, TSoloTarget). Mode 2: select the next mode (the call's index picks the text
// 3068 + index), 2.0 s, font 0; modes 4..11: extend the mode timer by 10 s when it is running; every branch scores the
// target (param 0), flashes v_lite607 and the order differs per branch as in the original.
void ModeSelectTargetControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	const int mode = info_text_box->MessageField;
	if (mode == 2)
	{
		const int index = SelectNextMode();
		const char* text = table_text::Get(3068 + (index <= 6 ? index : 7));
		if (!text)
		{
			std::fprintf(stderr, "FT rules: fatal: ModeSelectTargetControl: string resource unavailable\n");
			std::fflush(stderr);
			std::abort();
		}
		info_text_box->Display(text, 2.0f, 0, false);
		ScorePart(caller, 0);
		v_lite607->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		return;
	}
	if (mode < 4 || mode > 11)
	{
		v_lite607->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		ScorePart(caller, 0);
		return;
	}
	const int left = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
	if (left > 0)
	{
		// fild; fdiv qword 1000.0 (a double division stored as float); fadd dword 10.0f (0x438920 / 0x438914)
		const float seconds = static_cast<float>(static_cast<double>(left) / 1000.0);
		mode_timer->Message(MessageCode::TTimerResetTimer, seconds + 10.0f);
	}
	v_lite607->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
	ScorePart(caller, 0);
}
}
