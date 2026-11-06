#include "pch.h"
#include "rules/pirates_rules.h"
#include "table_text.h"
#include "TBlocker.h"
#include "TLight.h"
#include "TPinballTable.h"
#include "TSink.h"
#include "TSound.h"
#include "TTextBox.h"
#include <cstdio>
#include <cstdlib>

// Skulduggery rules: mode-running light, idle/launch mode entry, extra ball and jackpot awards,
// hold-sink release and the mode start / end light cleanup. Ported from port_listing_pirates.c lines 460-507, 658-668, 842-863,
// 1285-1309, 5674-5695, 5808-5846 (FT 0x423400-0x423460, 0x423680, 0x4239d0, 0x424700, 0x42a7d0, 0x42aad0, 0x42ab40).
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// get_rc_string(id): a missing resource is a load problem of the user's EXE, not something to skip.
const char* Text(int id)
{
	const char* text = table_text::Get(id);
	if (!text)
		Fatal("P-CORE: string resource unavailable");
	return text;
}

const float DisplayExtra = 2.0f;   // DAT_004388e4
const int ExtraBallText = 3014;
}

namespace pirates {
// 0x423400
void ModeRunningLightOn()
{
	v_lite606->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
}

// 0x423420
void ModeRunningLightOff()
{
	v_lite606->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x423440
void SetModeLaunch()
{
	ModeRunningLightOff();
	info_text_box->MessageField = 3;   // the original's direct write, not SetMode: keeps the per-player copies as they are
	MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
}

// 0x423460
void SetModeIdle()
{
	ModeRunningLightOff();
	info_text_box->MessageField = 2;
	MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
}

// 0x423680
void ReleaseHoldSink(float value)
{
	a_sink8->Message(MessageCode::TSinkResetTimer, value);
}

// 0x4239d0
float AwardExtraBall(float time)
{
	tabb00wave->Play(nullptr, "AwardExtraBall");
	const float shown = time + DisplayExtra;
	control::TableG->ExtraBalls = 1;
	info_text_box->Display(Text(ExtraBallText), shown, 0, false);
	return shown;
}

// 0x424700
float AwardJackpot(float time)
{
	if (time < 0.0f)
		time = -time;                  // a negative time = silent award
	else
		tabb85wave->Play(nullptr, "AwardJackpot");
	const float shown = time + DisplayExtra;
	info_text_box->Display("G", shown, 1, false);   // DAT_0043ab04: the one-character jackpot glyph (font 1)
	SpecialAddScore(nullptr, control::TableG->JackpotScore);
	return shown;
}

// 0x42a7d0
void table_add_extra_ball(float time)
{
	control::TableG->ExtraBalls = 1;
	tabb00wave->Play(nullptr, "table_add_extra_ball");
	if (0.0f < time)
		info_text_box->Display(Text(ExtraBallText), time, 0, false);
}

// 0x42aad0
void ModeStartCleanup()
{
	S().ModeCounter = 0;       // 0x435cf8
	S().TreasureXIndex = 0;    // 0x435d04
	SetHideawayLights(0);
	SetSkulduggeryLights(0);
	a_bloce->Message(MessageCode::TBlockerSetFrame, 1.0f);
	v_lite504->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	v_lite505->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	if (info_text_box->MessageField != 12)
		SetLavaLights(0);
}

// 0x42ab40
void ModeEndRestoreLights()
{
	RestoreHideawayLights();
	RestoreSkulduggeryTargets();
	RestoreLavaLights();
	ResetLduggTargets();
	ResetSkuTargets();
	ResetEryTargets();
	v_lite520->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite521->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}
}
