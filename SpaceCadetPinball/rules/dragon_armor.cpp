#include "pch.h"
#include "rules/dragon_rules.h"
#include "TBlocker.h"
#include "TDiverter.h"
#include "TKickback.h"
#include "TKickout.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"
#include "table_text.h"
#include <cstdio>
#include <cstdlib>

// Dragon's Keep rules: the weapon, armor and egg awards, the center and armor posts, the outlane rollovers,
// the outlane kickback, the right flipper's kickout-3 release and v_lite37's timer (port of DRAGON.EXE 0x423240, 0x4232b0,
// 0x423330, 0x4234b0, 0x425910, 0x425a10, 0x426a50, 0x426b00, 0x426b70, 0x426c60, 0x426d00, 0x428240).
namespace dragon {
namespace {
// The glyph strings of the meter font (font slot 1) at DRAGON.EXE 0x439a60..0x439ac4: code constants, not game text.
const char* const EggMeter[9] = {"", "#", "##", "###", "####", "#####", "######", "#######", "########"}; // 0x439a60, 9-byte stride
const char GlyphWeapon2[] = "%";   // 0x439ab8
const char GlyphWeapon3[] = "&";   // 0x439aba
const char GlyphWeapon4[] = "'";   // 0x439abc
const char GlyphWeapon5[] = "(";   // 0x439abe
const char GlyphKickback[] = "+";  // 0x439ac0
const char GlyphCenterPost[] = "*";// 0x439ac2
const char GlyphArmorPost[] = ")"; // 0x439ac4

[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

void ShowGlyphs(const char* glyphs)
{
	info_text_box->Display(glyphs, 2.0f, 1, false);   // TTextBox::Display(info_text_box, text, 2.0, font 1, 0)
}

// AwardArmorKickback(int) of FT 0x423240 ("ret 4"): the argument is a show-the-award flag. AwardRandomArmor passes 1; WellSinkControl
// and Mode9Controller (0x426444, 0x42a574, 0x42a76f) pass 0, which is the header's no-argument form below.
void ArmorKickback(int showText)
{
	a_kick1->ActiveFlag = 1;
	v_lite37->MessageField = 0;
	v_lite37->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
	outlane_kicker_light_group->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	if (showText)
	{
		ShowGlyphs(GlyphKickback);
		tabc41wave->Play(nullptr, "AwardArmorKickback");
	}
}

// CenterPostControl (0x425910) and ArmorPostControl (0x425a10) differ only in the light: the same body on (post, light).
void PostControl(MessageCode code, TBlocker* post, TLight* light)
{
	if (code == MessageCode::TBlockerDisable)
	{
		post->MessageField = 0;
		post->Message(MessageCode::TBlockerDisable, 0.0f);
		light->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		return;
	}
	if (code == MessageCode::TBlockerEnable)
	{
		post->MessageField = 1;
		post->Message(MessageCode::TBlockerEnable, 0.0f);
		post->Message(MessageCode::TBlockerRestartTimeout, static_cast<float>(post->InitialDuration));
		light->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		return;
	}
	if (code != MessageCode::ControlTimerExpired)
		return;
	if (post->MessageField == 1)
	{
		post->MessageField = 2;
		post->Message(MessageCode::TBlockerRestartTimeout, static_cast<float>(post->ExtendedDuration));
		light->Message(MessageCode::TLightFlasherStartTimedThenStayOff, static_cast<float>(post->ExtendedDuration));
		return;
	}
	post->MessageField = 0;
	post->Message(MessageCode::TBlockerDisable, 0.0f);
}

// The two outlane rollovers a_roll6 / a_roll1 (0x426b00 / 0x426b70): the same body on (rollover, light).
void OutlaneRollover(MessageCode code, TPinballComponent* caller, TLight* light)
{
	if (code != MessageCode::ControlCollision)
		return;
	if (light->light_on())
	{
		tabc24wave->Play(nullptr, "OutlaneRolloverControl");
		outlane_kicker_light_group->Message(MessageCode::TLightGroupOffsetAnimationForward, 0.0f);
		a_kick1->ActiveFlag = 1;
		light->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	}
	ScorePart(caller, 0);
}
}

void AwardArmorKickback() { ArmorKickback(0); }

void AddDragonEggs(int n)
{
	for (int i = 0; i < n; i++)   // the original counts down from n; a count of 0 or less adds none
		dragon_egg_light_group->Message(MessageCode::TLightGroupResetAndTurnOn, 1.0f);
	tabc27wave->Play(nullptr, "AddDragonEggs");
	const int lit = dragon_egg_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	if (lit < 0 || lit > 8)
		Fatal("AddDragonEggs: egg count outside the 9-entry meter table");
	ShowGlyphs(EggMeter[lit]);
	ScoreValue(nullptr, lit * 100000);
}

void AwardNextWeapon()
{
	const char* glyph;
	if (!v_lite33->light_on())
	{
		v_lite33->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		control::TableG->ScoreMultiplier = 2;
		SpecialAddScore(nullptr, 25000);
		glyph = GlyphWeapon2;
	}
	else if (!v_lite34->light_on())
	{
		v_lite34->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		control::TableG->ScoreMultiplier = 3;
		SpecialAddScore(nullptr, 35000);
		glyph = GlyphWeapon3;
	}
	else if (!v_lite35->light_on())
	{
		v_lite35->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		control::TableG->ScoreMultiplier = 4;
		SpecialAddScore(nullptr, 45000);
		glyph = GlyphWeapon4;
	}
	else if (!v_lite36->light_on())
	{
		v_lite36->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		control::TableG->ScoreMultiplier = 5;
		SpecialAddScore(nullptr, 55000);
		glyph = GlyphWeapon5;
	}
	else
		glyph = nullptr;   // all four weapons lit: only the sound
	if (glyph)
		ShowGlyphs(glyph);
	tabc39wave->Play(nullptr, "AwardNextWeapon");
}

void AwardRandomArmor()
{
	int unawarded[3];
	int count = 0;
	if (!v_lite37->light_on())
		unawarded[count++] = 0;
	if (!v_lite38->light_on())
		unawarded[count++] = 1;
	if (!v_lite39->light_on())
		unawarded[count++] = 2;
	if (count == 0)
		return;
	const int pick = unawarded[ft::RandIndex(count)];
	if (pick == 0)
	{
		ArmorKickback(1);
		ScoreValue(nullptr, 25000);
	}
	else if (pick == 1)
	{
		tabc40wave->Play(nullptr, "AwardRandomArmor");
		CenterPostControl(MessageCode::TBlockerEnable, a_center_post);
		ShowGlyphs(GlyphCenterPost);
		tabc40wave->Play(nullptr, "AwardRandomArmor");
		ScoreValue(nullptr, 50000);
	}
	else
	{
		tabc42wave->Play(nullptr, "AwardRandomArmor");
		ArmorPostControl(MessageCode::TBlockerEnable, a_bloce);
		ShowGlyphs(GlyphArmorPost);
		tabc42wave->Play(nullptr, "AwardRandomArmor");
		ScoreValue(nullptr, 100000);
	}
}

void CenterPostControl(MessageCode code, TPinballComponent* caller) { PostControl(code, static_cast<TBlocker*>(caller), v_lite38); }
void ArmorPostControl(MessageCode code, TPinballComponent* caller) { PostControl(code, static_cast<TBlocker*>(caller), v_lite39); }

void OutlaneKickerRolloverControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	ScorePart(caller, 0);
	tabc24wave->Play(nullptr, "OutlaneKickerRolloverControl");
	outlane_kicker_light_group->Message(MessageCode::TLightGroupOffsetAnimationForward, 0.0f);
	a_kick1->ActiveFlag = 1;
	v_lite47->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	a_dvtraa->Message(MessageCode::TGateDisable, 0.0f);
	a_dvtrab->Message(MessageCode::TGateEnable, 0.0f);
}

void OutlaneRolloverControl1(MessageCode code, TPinballComponent* caller) { OutlaneRollover(code, caller, v_lite56); }
void OutlaneRolloverControl2(MessageCode code, TPinballComponent* caller) { OutlaneRollover(code, caller, v_lite55); }

void RightFlipperControl(MessageCode code, TPinballComponent*)
{
	if (code != MessageCode::TFlipperExtend || a_kout3->MessageField != 1)
		return;
	const int mode = info_text_box->MessageField;
	a_kout3->MessageField = 0;
	if (mode == 9)
	{
		v_lite29->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		v_lite30->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
	}
	else
	{
		v_lite29->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite30->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	}
	a_kout3->Message(MessageCode::TKickoutRestartTimer, 0.0f);
}

void OutlaneKickbackControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlTimerExpired)
		return;
	if (!v_lite37->light_on())
	{
		outlane_kicker_light_group->Message(MessageCode::TLightGroupOffsetAnimationBackward, 0.0f);
		if (outlane_kicker_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f) == 0)
			caller->ActiveFlag = 0;
	}
	const char* text = table_text::Get(3009);   // the string comes from the user's EXE at run time
	if (!text)
	{
		std::fprintf(stderr, "FT rules: text 3009 unavailable (EXE missing or string absent), OutlaneKickbackControl shows nothing\n");
		return;
	}
	info_text_box->Display(text, 2.0f, 0, false);
}

void OutlaneKickerLightControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlTimerExpired)
		return;
	const int step = caller->MessageField;
	caller->MessageField = step + 1;
	if (step == 0)
		caller->Message(MessageCode::TLightTurnOnTimed, 53.0f);
	else if (step != 1)
		caller->MessageField = 0;
	else
		caller->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 5.0f);
}
}
