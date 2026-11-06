#include "pch.h"
#include "rules/dragon_rules.h"
#include "TBlocker.h"
#include "TBumper.h"
#include "TDiverter.h"
#include "TDrain.h"
#include "TFlipper.h"
#include "THole.h"
#include "TSoloTarget.h"
#include "TKickback.h"
#include "TLightGroup.h"
#include "TKickout.h"
#include "TLight.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TSink.h"
#include "TSound.h"
#include "TTextBox.h"
#include "midi.h"
#include "pb.h"
#include "table_text.h"
#include <cstdio>
#include <cstdlib>

// Dragon's Keep rules: the ball's life cycle (port of DRAGON.EXE 0x4245b0 PlungerControl, 0x424900
// BallDrainControl, 0x426d80 / 0x426e40 Kickout1/2Control, 0x4285f0 Mode10LaunchBallController, 0x428800
// Mode2WaitQuestController, 0x42bf60 Mode0GameOverController). Listing lines 1173-1251, 1282-1363, 2904-2962,
// 4248-4377, 5965-5995 of port_listing_dragon.c; where Ghidra shows a stack artefact as a receiver (Kickout1's receiver
// is the caller) the disassembly was read.
namespace dragon {
namespace {
constexpr int K(MessageCode code) { return static_cast<int>(code); }

[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

void ShowInfoText(int id, float time)
{
	const char* text = table_text::Get(id);
	if (!text)
	{
		std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), nothing shown\n", id);
		return;
	}
	info_text_box->Display(text, time, 0, false);
}

void DrainTimerExpired()
{
	DragonState& st = S();
	if (st.LastBall != 0)
		return;
	if (st.DrainState == 2)
		s_plunger1->Message(MessageCode::PlungerStartFeedTimer, 0.96f);
	else if (st.DrainState == 3)
		s_plunger1->Message(MessageCode::PlungerRelaunchBall, 0.96f);
}

void ReleaseLockedBall(TLight* light, TSink* sink)
{
	info_text_box->MessageField = 1;
	control::TableG->MultiballCount++;
	sink->Message(MessageCode::TSinkResetTimer, 0.0f);
	light->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}
}

void PlungerControl(MessageCode code, TPinballComponent*)
{
	if (code == MessageCode::PlungerFeedBall)
	{
		MissionControl(K(MessageCode::ControlMissionStarted), nullptr);
		return;
	}
	if (code != MessageCode::PlungerStartFeedTimer)
		return;
	DragonRules& r = R();
	DragonState& st = r.St;
	st.BallDrained = 0;
	st.Vestige = 0;
	ResetSpells();
	UpdateTowerLocks();
	CloseDragonJaw(1);
	CenterPostControl(MessageCode::TBlockerDisable, a_center_post);
	ArmorPostControl(MessageCode::TBlockerDisable, a_bloce);
	a_kick1->ActiveFlag = 1;
	v_lite37->MessageField = 0;
	v_lite37->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	outlane_kicker_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	outlane_kicker_light_group->Message(MessageCode::TLightGroupOffsetAnimationForward, 0.0f);
	v_lite33->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite34->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite35->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite36->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	control::TableG->ScoreMultiplier = 1;
	if (info_text_box->MessageField != 6 && info_text_box->MessageField != 0xd)
	{
		v_lite47->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_dvtraa->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrab->Message(MessageCode::TGateDisable, 0.0f);
	}
	a_dvtrba->Message(MessageCode::TGateDisable, 0.0f);
	a_dvtrbb->Message(MessageCode::TGateEnable, 0.0f);
	a_dvtrca->Message(MessageCode::TGateDisable, 0.0f);
	a_dvtrcb->Message(MessageCode::TGateEnable, 0.0f);
	if (info_text_box->MessageField != 6 && info_text_box->MessageField != 0xd)
	{
		v_lite125->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite126->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		a_dvtrda->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrdb->Message(MessageCode::TGateDisable, 0.0f);
	}
	SetSwordState(3);
	v_lite46->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_rockb->Message(MessageCode::TBlockerEnable, 0.0f);
	v_lite45->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_rocka->Message(MessageCode::TBlockerEnable, 0.0f);
	gargoyle_spot_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite67->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite70->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite68->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite68->MessageField = 0;
	v_lite69->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite69->MessageField = 0;
	spell_spot_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_bump1->MessageField = 0;
	st.TargetsHit = 0;
	st.Spells = 0;
	r.C.LastScorer = nullptr;
	if (SelectRandomQuest() != 9)
	{
		info_text_box->MessageField = 10;
		MissionControl(K(MessageCode::ControlMissionComplete), nullptr);
	}
	v_lite40->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	r.C.BallSave = 3;
}

void BallDrainControl(MessageCode code, TPinballComponent* caller)
{
	DragonRules& r = R();
	DragonState& st = r.St;
	TPinballTable* table = control::TableG;
	if (code == MessageCode::ControlTimerExpired)
	{
		DrainTimerExpired();
		return;
	}
	if (code != MessageCode::ControlCollision)
		return;
	st.DrainState = 1;
	if (table->TiltLockFlag != 0)
	{
		v_lite40->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		r.C.BallSave = 0;
	}
	if (v_lite40->light_on())
	{
		st.DrainState = 3;
		drain->Message(MessageCode::SetTiltLock, 0.0f);
		table->BallInDrainFlag = 1;
		DrainTimerExpired();
		return;
	}
	MissionControl(K(MessageCode::ControlBallDrained), caller);
	if (table->MultiballCount == 0 && table->ExtraBalls == 0 && table->BallCount == 1 &&
		table->CurrentPlayer - table->PlayerCount == -1 && st.LastBall == 0)
	{
		st.LastBall = table->TiltLockFlag == 0 ? 1 : 2;
		table->TiltLockFlag = 1;
		a_flip1->Message(MessageCode::SetTiltLock, 0.0f);
		a_flip3->Message(MessageCode::SetTiltLock, 0.0f);
		a_flip2->Message(MessageCode::SetTiltLock, 0.0f);
		if (v_lite50->LightOnFlag != 0)
			ReleaseLockedBall(v_lite50, a_sink16);
		if (v_lite49->LightOnFlag != 0)
			ReleaseLockedBall(v_lite49, a_sink17);
		if (v_lite48->LightOnFlag != 0)
			ReleaseLockedBall(v_lite48, a_sink18);
	}
	if (table->MultiballCount != 0 && st.LastBall == 0)
	{
		st.DrainState = 0;
		return;
	}
	if (info_text_box->MessageField == 0xb || info_text_box->MessageField == 0)
		return;
	info_text_box->MessageField = 0xb;
	MissionControl(K(MessageCode::ControlMissionComplete), nullptr);
}

void Kickout1Control(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField != 7)
	{
		AwardRandomArmor();
		v_lite14->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite13->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite12->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite11->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite45->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_rocka->Message(MessageCode::TBlockerEnable, 2.0f);
		caller->Message(MessageCode::TKickoutRestartTimer, -1.0f);
		return;
	}
	AwardJackpot(0.0f, 1);
	caller->Message(MessageCode::TKickoutRestartTimer, -1.0f);
}

void Kickout2Control(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField == 2)
	{
		info_text_box->MessageField = 14;
		MissionControl(K(MessageCode::ControlMissionComplete), nullptr);
		return;
	}
	tabc32wave->Play(nullptr, "Kickout2Control");
	if (info_text_box->MessageField != 7)
	{
		if (caller->MessageField < 3)
			caller->MessageField++;
		ScorePart(caller, caller->MessageField);
		a_kout2->Message(MessageCode::TKickoutRestartTimer, -1.0f);
		return;
	}
	AddDragonEggs(1);
	caller->Message(MessageCode::TKickoutRestartTimer, -1.0f);
}

void Mode10LaunchBallController(int code, TPinballComponent* caller)
{
	if (code == K(MessageCode::ControlBallCaptured))
	{
		if (caller == s_hole118)
		{
			StartSelectedQuest();
			return;
		}
	}
	else if (code == K(MessageCode::ControlCollision))
	{
		if (!caller)
			Fatal("Mode10LaunchBallController: collision without a caller");
		switch (caller->TypeId)
		{
		case 1000: case 0x3ed: case 0x3ee: case 0x3f4: case 0x3f6: case 0x3f7: case 0x3f9: case 0x3fa:
		case 0x3fc: case 0x3fd: case 0x400: case 0x406:
			break; // scores 15,000 below
		case 0x3ef:
			info_text_box->MessageField = 2;
			MissionControl(K(MessageCode::ControlMissionComplete), nullptr);
			MissionControl(K(MessageCode::ControlMissionStarted), nullptr);
			return;
		case 0x3fb: // TSoloTarget
			if (caller == a_spot4)
			{
				const int spell = PickUnlitSpell();
				if (spell != -1)
					AwardSpell(spell);
				info_text_box->MessageField = 2;
				MissionControl(K(MessageCode::ControlMissionComplete), nullptr);
				return;
			}
			if (caller == a_spot5)
			{
				AddDragonEggs(1);
				info_text_box->MessageField = 2;
				MissionControl(K(MessageCode::ControlMissionComplete), nullptr);
				return;
			}
			if (caller == a_spot6)
			{
				AwardNextWeapon();
				info_text_box->MessageField = 2;
				MissionControl(K(MessageCode::ControlMissionComplete), nullptr);
				return;
			}
			if (caller == a_spot7)
			{
				AwardRandomArmor();
				info_text_box->MessageField = 2;
				MissionControl(K(MessageCode::ControlMissionComplete), nullptr);
				return;
			}
			if (caller == a_spot22)
			{
				info_text_box->MessageField = 14;
				MissionControl(K(MessageCode::ControlMissionComplete), nullptr);
				return;
			}
			break;
		default:
			return;
		}
	}
	else if (code == K(MessageCode::ControlMissionComplete))
	{
		S().BallHeld = 1;
		ShowInfoText(3066, -1.0f);
		return;
	}
	else if (code == K(MessageCode::ControlMissionStarted))
	{
		ShowInfoText(3066, -1.0f);
		return;
	}
	else
		return;
	if (control::TableG->TiltLockFlag == 0)
		ScoreValue(nullptr, 15000);
	info_text_box->MessageField = 2;
	MissionControl(K(MessageCode::ControlMissionComplete), nullptr);
	MissionControl(K(MessageCode::ControlMissionStarted), nullptr);
}

void Mode2WaitQuestController(int code)
{
	DragonState& st = S();
	if (code == K(MessageCode::ControlMissionComplete))
	{
		if (st.BallHeld == 0)
			info_text_box->Clear();
		st.BallHeld = 0;
		if (midi::active_track() != st.TrackNewGame)
			PlayTrack(st.TrackNewGame);
	}
	else if (code == K(MessageCode::ControlMissionStarted))
		ShowInfoText(3001, -1.0f);
}

void Mode0GameOverController(int code)
{
	if (code != K(MessageCode::ControlMissionComplete))
		return;
	DragonState& st = S();
	if (midi::active_track() != st.TrackGameOver)
		PlayTrack(st.TrackGameOver);
	control::TableG->Message(MessageCode::GameOver, 0.0f);
	if (pb::chk_highscore())
	{
		tabc56wave->Play(nullptr, "Mode0GameOverController");
		control::TableG->LightGroup->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 3.0f);
		ShowInfoText(3065, -1.0f);
	}
}
}
