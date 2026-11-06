#include "pch.h"
#include "rules/dragon_rules.h"
#include "TAccelerator.h"
#include "TBlocker.h"
#include "TDiverter.h"
#include "TFlipper.h"
#include "THole.h"
#include "TKickout.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TRampExit.h"
#include "TSink.h"
#include "TSound.h"
#include "TTextBox.h"
#include "table_text.h"
#include <cstdio>
#include <cstdlib>

// Dragon's Keep rules: the egg shop and the lower-left parts around it (port of DRAGON.EXE 0x425b10
// BlockerFControl, 0x426140 WellSinkControl, 0x426940 AcceleratorControl, 0x426be0 / 0x426c20 the dragon egg ramp exits,
// 0x426ee0 HoleControl, 0x427ef0 Kickout3Control, 0x428290 Kickout3LightControl, 0x42ac80 Mode12EggShopController).
// Rule globals used: EggOffer 0x434d1c, EggSpell 0x434d28, BallHeld 0x439b10 (DragonState).
namespace dragon {
namespace {
// TTextBox::Display(info_text_box, get_rc_string(id), time, 0, 0). The text comes from the user's EXE at run time;
// when it is unavailable nothing is shown (and the reader has logged why), never a substitute text.
void ShowText(int id, float time)
{
	const char* text = table_text::Get(id);
	if (!text)
	{
		static bool logged = false;
		if (!logged)
			std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), shop shows nothing\n", id);
		logged = true;
		return;
	}
	info_text_box->Display(text, time, 0, false);
}
}

void BlockerFControl(MessageCode code, TPinballComponent* caller)
{
	TBlocker* blocker = static_cast<TBlocker*>(caller);
	if (code == MessageCode::TBlockerDisable)
	{
		blocker->MessageField = 1;
		blocker->Message(MessageCode::TBlockerDisable, 0.0f);
		blocker->Message(MessageCode::TBlockerRestartTimeout, static_cast<float>(blocker->InitialDuration));
		v_lite32->Message(MessageCode::TLightTurnOnTimed, static_cast<float>(blocker->InitialDuration));
		return;
	}
	if (code == MessageCode::TBlockerEnable)
	{
		blocker->Message(MessageCode::TBlockerRestartTimeout, -1.0f);
		v_lite32->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		blocker->MessageField = 0;
		blocker->Message(MessageCode::TBlockerEnable, 0.0f);
		return;
	}
	if (code != MessageCode::ControlTimerExpired)
		return;
	if (blocker->MessageField == 1)
	{
		blocker->MessageField = 2;
		blocker->Message(MessageCode::TBlockerRestartTimeout, static_cast<float>(blocker->ExtendedDuration));
		v_lite32->Message(MessageCode::TLightFlasherStartTimed, static_cast<float>(blocker->ExtendedDuration));
		return;
	}
	blocker->MessageField = 0;
	blocker->Message(MessageCode::TBlockerEnable, 0.0f);
}

void WellSinkControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	DragonState& st = S();
	if (info_text_box->MessageField == 2)
	{
		if (ft::RandIndex(4) == 1)
		{
			st.BallHeld = 1;
			if (StartSelectedQuest() == 0)
			{
				st.BallHeld = 0;
				a_sink4->Message(MessageCode::TSinkResetTimer, -1.0f);
			}
		}
		else
			AwardRandomSpell();
		a_sink4->Message(MessageCode::TSinkResetTimer, -1.0f);
		well_light_group->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		spell_spot_light_group->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		return;
	}
	if (info_text_box->MessageField != 8)
	{
		AwardRandomSpell();
		a_sink4->Message(MessageCode::TSinkResetTimer, -1.0f);
		well_light_group->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		spell_spot_light_group->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		return;
	}
	// Mode 8: the award given is chosen by the well's state; then the next state is drawn.
	int draw;
	int state;
	int textId;
	switch (a_sink4->MessageField)
	{
	case 0:
		table_add_extra_ball(2.0f);
		draw = ft::RandIndex(4);
		// 0 -> state 1 (3040); 1 -> state 2 (3041); 2 -> state 3 (3042); 3 -> state 4 (3043)
		state = draw == 0 ? 1 : draw == 1 ? 2 : draw == 2 ? 3 : 4;
		break;
	case 1:
		v_lite33->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite34->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite35->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite36->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		control::TableG->ScoreMultiplier = 5;
		ShowText(3045, 2.0f);
		draw = ft::RandIndex(4);
		// 0 -> state 0 (3039); 1 -> state 2 (3041); 2 -> state 3 (3042); 3 -> state 4 (3043)
		state = draw == 0 ? 0 : draw == 1 ? 2 : draw == 2 ? 3 : 4;
		break;
	case 2:
		AwardArmorKickback();
		tabc24wave->Play(nullptr, "WellSinkControl");
		CenterPostControl(MessageCode::TBlockerEnable, a_center_post);
		ArmorPostControl(MessageCode::TBlockerEnable, a_bloce);
		ShowText(3044, 2.0f);
		draw = ft::RandIndex(4);
		// 0 -> state 0 (3039); 1 -> state 1 (3040); 2 -> state 3 (3042); 3 -> state 4 (3043)
		state = draw == 0 ? 0 : draw == 1 ? 1 : draw == 2 ? 3 : 4;
		break;
	case 3:
		AwardJackpot(0.0f, 1);
		draw = ft::RandIndex(4);
		// 0 -> state 0 (3039); 1 -> state 1 (3040); 2 -> state 2 (3041); 3 -> state 4 (3043)
		state = draw == 0 ? 0 : draw == 1 ? 1 : draw == 2 ? 2 : 4;
		break;
	default:
		ScoreValue(caller, 1000000);
		ShowText(3046, 2.0f);
		draw = ft::RandIndex(4);
		// 0 -> state 0 (3039); 1 -> state 1 (3040); 2 -> state 2 (3041); 3 -> state 3 (3042)
		state = draw == 0 ? 0 : draw == 1 ? 1 : draw == 2 ? 2 : 3;
		break;
	}
	textId = 3039 + state; // 0xbdf + state: 3039, 3040, 3041, 3042, 3043
	a_sink4->MessageField = state;
	ShowText(textId, 4.0f);
	a_sink4->Message(MessageCode::TSinkResetTimer, -1.0f);
}

void AcceleratorControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision && info_text_box->MessageField != 4)
	{
		tabc22wave->Play(nullptr, "AcceleratorControl");
		ScorePart(caller, v_lite32->light_on() ? 1 : 0);
	}
}

void DragonEggRampExitControl2(MessageCode code, TPinballComponent*)
{
	if (code != MessageCode::ControlBallToLayer1)
		return;
	AddDragonEggs(2);
	if (info_text_box->MessageField == 2)
	{
		a_dvtrca->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrcb->Message(MessageCode::TGateDisable, 0.0f);
	}
}

void DragonEggRampExitControl1(MessageCode code, TPinballComponent*)
{
	if (code != MessageCode::ControlBallToLayer1)
		return;
	AddDragonEggs(1);
	if (info_text_box->MessageField == 2)
	{
		a_dvtrba->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrbb->Message(MessageCode::TGateDisable, 0.0f);
	}
}

void HoleControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlBallCaptured)
	{
		if (info_text_box->MessageField == 2)
		{
			if (control::TableG->MultiballCount == 1)
			{
				info_text_box->MessageField = 12;
				MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
				return;
			}
		}
		else if (info_text_box->MessageField == 10)
		{
			caller->MessageField = 2;
			return;
		}
		tabc32wave->Play(nullptr, "HoleControl");
		caller->MessageField = 1;
		caller->Message(MessageCode::THoleEjectBall, 1.0f);
		return;
	}
	if (code != MessageCode::ControlBallReleased)
		return;
	if (info_text_box->MessageField == 2 && caller->MessageField == 0)
		tabc34wave->Play(nullptr, "HoleControl");
}

void Kickout3Control(MessageCode code, TPinballComponent*)
{
	if (code == MessageCode::ControlCollision)
		a_kout3->MessageField = 1;
}

void Kickout3LightControl(MessageCode code, TPinballComponent*)
{
	if (code == MessageCode::ControlTimerExpired && v_lite29->light_on())
		a_kout3->ActiveFlag = 1;
}

namespace {
// The egg offer after a mission-complete: the count of lit eggs, lowered while the better offers' conditions fail
// (the original's chain of gotos, 0x42ad0b..): 8 extra ball, 7 the extra-ball light, 6 the three lights 37..39, 5 the four
// lights 33..36, 4 a spell, 1..3 the quest. EggOffer is written at every step, as in the original.
void ResolveOffer(DragonState& st, int litEggs)
{
	st.EggOffer = litEggs;
	switch (st.EggOffer)
	{
	case 1: case 2: case 3:
		st.EggOffer = 1;
		return;
	case 4: case 5: case 6: case 7: case 8:
		break;
	default:
		st.EggOffer = 0;
		return;
	}
	if (st.EggOffer == 8)
	{
		if (control::TableG->ExtraBalls == 0)
			return;
		st.EggOffer = 7;
	}
	if (st.EggOffer == 7)
	{
		if (!v_lite57->light_on())
			return;
		st.EggOffer = 6;
	}
	if (st.EggOffer == 6)
	{
		if (!(v_lite37->light_on() && v_lite38->light_on() && v_lite39->light_on()))
			return;
		st.EggOffer = 5;
	}
	if (st.EggOffer == 5)
	{
		if (!(v_lite33->light_on() && v_lite34->light_on() && v_lite35->light_on() && v_lite36->light_on()))
			return;
		st.EggOffer = 4;
	}
	// EggOffer == 4
	st.EggSpell = PickUnlitSpell();
	if (st.EggSpell == -1)
		st.EggOffer = 1;
}

// A flipper button press in mode 12: the player takes (a_flip1) or declines (the other flipper) the offer.
void ShopAnswer(DragonState& st, TPinballComponent* caller)
{
	info_text_box->MessageField = 2;
	info_text_box->Clear();
	if (caller == a_flip1)
	{
		switch (st.EggOffer)
		{
		case 1: case 2: case 3:
			st.BallHeld = 1;
			if (StartSelectedQuest() != 0)
			{
				dragon_egg_light_group->Message(MessageCode::TLightGroupResetAndTurnOff, 1.0f);
				return;
			}
			st.BallHeld = 0;
			break;
		case 4:
			AwardSpell(st.EggSpell);
			break;
		case 5:
			AwardNextWeapon();
			break;
		case 6:
			AwardRandomArmor();
			break;
		case 7:
			tabc35wave->Play(nullptr, "Mode12EggShopController");
			v_lite57->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			break;
		default:
			table_add_extra_ball(2.0f);
		}
		// while (n = EggOffer != 0, EggOffer--, n): one reset per egg, EggOffer ends at -1 (bounded: EggOffer <= 8)
		while (st.EggOffer-- != 0)
			dragon_egg_light_group->Message(MessageCode::TLightGroupResetAndTurnOff, 1.0f);
		s_hole118->MessageField = 1;
	}
	else
	{
		ScoreValue(nullptr, 25000);
		s_hole118->MessageField = 0;
	}
	s_hole118->Message(MessageCode::THoleEjectBall, 0.0f);
	st.BallHeld = 1;
	MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
}
}

void Mode12EggShopController(int code, TPinballComponent* caller)
{
	DragonState& st = S();
	if (code == static_cast<int>(MessageCode::TFlipperExtend))
	{
		ShopAnswer(st, caller);
		return;
	}
	if (code == static_cast<int>(MessageCode::ControlMissionComplete))
	{
		s_hole118->MessageField = 0;
		info_text_box->Clear();
		ResolveOffer(st, dragon_egg_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f));
	}
	else if (code != static_cast<int>(MessageCode::ControlMissionStarted))
		return;

	int textId;
	switch (st.EggOffer)
	{
	case 0:
		tabc32wave->Play(nullptr, "Mode12EggShopController");
		ScoreValue(nullptr, 25000);
		s_hole118->MessageField = 0;
		ShowText(3068, 2.0f);
		s_hole118->Message(MessageCode::THoleEjectBall, 2.0f);
		st.BallHeld = 1;
		info_text_box->MessageField = 2;
		MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
		return;
	case 1: case 2: case 3:
		switch (a_kout2->MessageField)
		{
		case 4: textId = 3057; break;
		case 5: textId = 3058; break;
		case 6: textId = 3059; break;
		case 7: textId = 3060; break;
		case 8: textId = 3061; break;
		default: textId = 0; break; // no text; the sound below still follows
		}
		break;
	case 4: textId = 3056; break;
	case 5: textId = 3055; break;
	case 6: textId = 3054; break;
	case 7: textId = 3053; break;
	default: textId = 3052; break;
	}
	if (textId)
		ShowText(textId, -1.0f);
	if (code == static_cast<int>(MessageCode::ControlMissionComplete))
		tabc33wave->Play(nullptr, "Mode12EggShopController");
}
}
