#include "pch.h"
#include "rules/pirates_rules.h"
#include "table_text.h"
#include "TBlocker.h"
#include "TKickback.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TMagnet.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TWall.h"
#include <cstdio>
#include <cstdlib>

// Skulduggery rules: the two flipper control functions, the outlane kickback (light and
// kickback control), the two kickouts and the six magnets. Ported from port_listing_pirates.c lines 5221-5292 and 5360-5673
// (FT 0x42a1e0, 0x42a220, 0x42a260, 0x42a2a0, 0x42a420, 0x42a470 .. 0x42a740, PIRATES.EXE). Every receiver, code and value
// below was re-read in machine code (capstone): the listing's `piVar1 = 0x39` in KickbackControl and the
// `iStack_8` receivers of the magnets are Ghidra stack artefacts; the receiver of those ScorePart /
// ScoreValue calls is the caller (param_2).
namespace pirates {
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// TTextBox::Display(text, time, font, low priority) as the original's four-argument call (FT 0x41de40).
void Show(const char* text, float time, int font)
{
	info_text_box->Display(text, time, font, false);
}

// get_rc_string(id) shown as it is (the kickout text has no conversion).
void ShowText(int stringId, float time)
{
	const char* text = table_text::Get(stringId);
	if (!text)
		Fatal("lower: table text id missing from the EXE");
	Show(text, time, 0);
}

// crt_sprintf(buf, get_rc_string(id), value) + Display(buf, time, 0, 0). The long cast satisfies %ld and %d alike on x86-64.
void ShowScoreText(int stringId, int value, float time)
{
	const char* format = table_text::Get(stringId);
	if (!format)
		Fatal("lower: table text id missing from the EXE");
	char buf[64];
	std::snprintf(buf, sizeof buf, format, static_cast<long>(value));
	Show(buf, time, 0);
}

// The three lane groups step back on every flipper press (a_flip1 and a_flip2 run the same body, FT 0x42a1e0 / 0x42a220).
void StepLanesBack(MessageCode code)
{
	if (code != MessageCode::TFlipperExtend)
		return;
	lane_winds_light_group->Message(MessageCode::TLightGroupStepBackward, 0.0f);
	island_lane_top_light_group->Message(MessageCode::TLightGroupStepBackward, 0.0f);
	island_lane_bottom_light_group->Message(MessageCode::TLightGroupStepBackward, 0.0f);
}

// One of the six magnets (MagnetControl1-6, FT 0x42a470 + 0x90 * n): the bodies differ only in the light (v_lite560..565) and
// the magnet part (s_magnet1..6). The sound is tabb87wave in all six (one global, FT 0x43c27c).
void MagnetBody(TLight* light, TMagnet* magnet, MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	tabb87wave->Play(nullptr, "MagnetControl");
	light->Message(MessageCode::TLightResetAndTurnOff, 2.0f);
	magnet->Message(MessageCode::FtPartDisable, 0.0f);
	ShowScoreText(3003, ScoreValue(caller, 100000), 2.0f);
}
}

// 0x42a1e0 LeftFlipperControl (a_flip1)
void LeftFlipperControl(MessageCode code, TPinballComponent*)
{
	StepLanesBack(code);
}

// 0x42a220 RightFlipperControl (a_flip2)
void RightFlipperControl(MessageCode code, TPinballComponent*)
{
	StepLanesBack(code);
}

// 0x42a260 KickbackLightControl (v_lite544): when the kickback light's timer expires with the light neither on, toggled nor
// flashing, the kickback and the wall behind it are deactivated.
void KickbackLightControl(MessageCode code, TPinballComponent*)
{
	if (code == MessageCode::ControlTimerExpired && !v_lite544->LightOnFlag && !v_lite544->ToggledOnFlag &&
	    !v_lite544->FlasherOnFlag)
	{
		a_kick2->ActiveFlag = 0;
		s_bwal610->ActiveFlag = 0;
	}
}

// 0x42a2a0 KickbackControl (a_kick2): on its timer the outlane gate closes (blockers a_blocc / a_bloce / a_blocf / a_blocg), the
// two arrow lights swap, the kickback light flashes once more and the part scores index 0.
void KickbackControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlTimerExpired)
		return;
	a_blocc->Message(MessageCode::TBlockerSetFrame, 0.0f);
	a_bloce->Message(MessageCode::TBlockerSetFrame, 1.0f);
	v_lite504->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	v_lite505->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_blocf->Message(MessageCode::TBlockerSetFrame, 0.0f);
	a_blocg->Message(MessageCode::TBlockerSetFrame, -1.0f);
	v_lite544->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	ScorePart(caller, 0);
}

// 0x42a420 KickoutControl (a_kout2, a_kout1): a ball in either kickout scores 1,000,000 (no scoring part: the last scorer is
// only replaced by none), shows string 3015 for 2.0 s and restarts the kicker timer with 1.0.
void KickoutControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	ScoreValue(nullptr, 1000000);
	ShowText(3015, 2.0f);
	caller->Message(MessageCode::TKickoutRestartTimer, 1.0f);
}

// 0x42a470 .. 0x42a740 MagnetControl1-6 (s_magnet1-6)
void MagnetControl1(MessageCode code, TPinballComponent* caller) { MagnetBody(v_lite560, s_magnet1, code, caller); }
void MagnetControl2(MessageCode code, TPinballComponent* caller) { MagnetBody(v_lite561, s_magnet2, code, caller); }
void MagnetControl3(MessageCode code, TPinballComponent* caller) { MagnetBody(v_lite562, s_magnet3, code, caller); }
void MagnetControl4(MessageCode code, TPinballComponent* caller) { MagnetBody(v_lite563, s_magnet4, code, caller); }
void MagnetControl5(MessageCode code, TPinballComponent* caller) { MagnetBody(v_lite564, s_magnet5, code, caller); }
void MagnetControl6(MessageCode code, TPinballComponent* caller) { MagnetBody(v_lite565, s_magnet6, code, caller); }
}
