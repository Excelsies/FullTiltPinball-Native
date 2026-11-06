#include "pch.h"
#include "rules/pirates_rules.h"
#include "midi.h"
#include "pb.h"
#include "table_text.h"
#include "TBlocker.h"
#include "TDrain.h"
#include "TFlipper.h"
#include "TKickback.h"
#include "TKickout.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TMagnet.h"
#include "TOneway.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "TWall.h"
#include <cstdio>
#include <cstdlib>

// Skulduggery rules: the plunger and drain control functions and the mode controllers of
// idle (2), launch / skill shot (3), bonus count (16), game over (1) and tilt (17). Ported from port_listing_pirates.c
// (FT 0x425e70, 0x426110, 0x42ab90, 0x42abf0, 0x42b820, 0x42bfd0, 0x42d730).
namespace pirates {
namespace {
using MC = MessageCode;

[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// get_rc_string: the original always has its string table; without it the rules cannot run (fail fast, no substitute text).
const char* Str(int id)
{
	const char* text = table_text::Get(id);
	if (!text)
		Fatal("string resource unavailable");
	return text;
}

int Code(MC c) { return static_cast<int>(c); }

// The skill-shot wheel glyph strings (FT 0x43aa58, 9 entries of 8 bytes): font-1 glyph codes, not text.
const char SkillGlyphs[9][8] = {
	{0x24, 0x26, 0x28, 0x29, 0x2c, 0x2e, 0x32, 0}, {0x22, 0x24, 0x26, 0x27, 0x2a, 0x2c, 0x2e, 0},
	{0x30, 0x22, 0x24, 0x25, 0x28, 0x2a, 0x2c, 0}, {0x32, 0x30, 0x22, 0x23, 0x26, 0x28, 0x2a, 0},
	{0x2e, 0x32, 0x30, 0x21, 0x24, 0x26, 0x28, 0}, {0x2c, 0x2e, 0x32, 0x2f, 0x22, 0x24, 0x26, 0},
	{0x2a, 0x2c, 0x2e, 0x31, 0x30, 0x22, 0x24, 0}, {0x28, 0x2a, 0x2c, 0x2d, 0x32, 0x30, 0x22, 0},
	{0x26, 0x28, 0x2a, 0x2b, 0x2e, 0x32, 0x30, 0}};

// One bonus-count step that has an amount (Mode16 cases 0..4): the total always grows; the text shows unless skipped.
// Returns true when the step displayed (the original returns, the next message resumes at the next step).
bool BonusStepText(int amount, int stringId, TSound* wave, const char* what)
{
	State& s = S();
	if (amount == 0)
		return false;
	s.BonusTotal += amount;
	if (s.BonusSkip != 0)
		return false;
	wave->Play(nullptr, what);
	char buf[64];
	std::snprintf(buf, sizeof buf, Str(stringId), static_cast<long>(amount));
	info_text_box->Display(buf, s.BatchFlag == 0 ? 2.0f : 0.25f, 0, false);
	return true;
}
}

// 0x425e70 PlungerControl (control function of s_launcher1)
void PlungerControl(MessageCode code, TPinballComponent*)
{
	State& s = S();
	int& ballSave = BallSave();
	if (code == MC::PlungerInputReleased)
	{
		if (control::TableG->TiltLockFlag == 0)
		{
			a_cannon->Message(MC::TBlockerSetFrame, 3.0f);
			a_cannon->Message(MC::TBlockerMoveToFrame, 0.0f);
		}
		return;
	}
	if (code == MC::PlungerFeedBall)
	{
		MissionControl(Code(MC::ControlMissionStarted), nullptr);
		switch (ballSave)
		{
		case 0:
			v_lite569->Message(MC::TLightResetAndTurnOff, 0.0f);
			return;
		case 1:
			if (!v_lite569->FlasherOnFlag)
				v_lite569->Message(MC::TLightFlasherStartTimed, 5.0f);
			return;
		case 2:
			if (!v_lite569->ToggledOnFlag)
				v_lite569->Message(MC::TLightTurnOnTimed, 10.0f);
			return;
		default:
			ballSave = 3;
			// fall through
		case 3:
			if (!v_lite569->LightOnFlag)
				v_lite569->Message(MC::TLightResetAndTurnOn, 0.0f);
			return;
		}
	}
	if (code != MC::PlungerStartFeedTimer)
		return;
	s.Vestige = 0;
	if (ballSave == 0)
	{
		LastScorer() = nullptr;
		info_text_box->MessageField = 3;
		MissionControl(Code(MC::ControlMissionComplete), nullptr);
		MagnetsOff();
		a_bloce->Message(MC::TBlockerSetFrame, 0.0f);
		v_lite504->Message(MC::TLightResetAndTurnOff, 0.0f);
		v_lite505->Message(MC::TLightResetAndTurnOn, 0.0f);
	}
	v_lite569->Message(MC::TLightResetAndTurnOn, 0.0f);
	ballSave = 3;
	v_lite544->Message(MC::TLightFlasherStartTimedThenStayOn, 1.0f);
	a_kick2->ActiveFlag = 1;
	s_bwal610->ActiveFlag = 1;
	fathom_light_group->Message(MC::TLightResetAndTurnOff, 0.0f);
	control::TableG->ScoreMultiplier = 1;
	a_kick1->Message(MC::FtPartDisable, 0.0f);
	a_bloce->Message(MC::TBlockerSetFrame, 0.0f);
	v_lite504->Message(MC::TLightResetAndTurnOff, 0.0f);
	v_lite505->Message(MC::TLightResetAndTurnOn, 0.0f);
}

// 0x426110 BallDrainControl (control function of drain)
void BallDrainControl(MessageCode code, TPinballComponent* caller)
{
	State& s = S();
	TPinballTable* table = control::TableG;
	for (;;)
	{
		if (code == MC::ControlTimerExpired)
		{
			if (s.LastBall != 0)
				return;
			if (s.DrainState == 2)
				s_launcher1->Message(MC::PlungerStartFeedTimer, 0.96f);
			else if (s.DrainState == 3)
				s_launcher1->Message(MC::PlungerRelaunchBall, 0.96f);
			return;
		}
		if (code != MC::ControlCollision)
			return;
		s.DrainState = 1;
		if (table->TiltLockFlag != 0)
		{
			v_lite569->Message(MC::TLightResetAndTurnOff, 0.0f);
			BallSave() = 0;
		}
		if (!v_lite569->light_on())
			break;
		// ball saved: show the text, relaunch through the timer-expired branch above
		info_text_box->Display(Str(3026), 2.0f, 0, false);
		s.DrainState = 3;
		drain->Message(MC::Reset, 0.0f);
		code = MC::ControlTimerExpired;
		table->BallInDrainFlag = 1;
	}
	MissionControl(Code(MC::ControlBallDrained), caller);
	if (table->MultiballCount == 0 && table->ExtraBalls == 0 && table->BallCount == 1
		&& table->CurrentPlayer - table->PlayerCount == -1 && s.LastBall == 0)
	{
		s.LastBall = (table->TiltLockFlag != -1);
		table->TiltLockFlag = 1;
		a_flip1->Message(MC::SetTiltLock, 0.0f);
		a_flip3->Message(MC::SetTiltLock, 0.0f);
		a_flip2->Message(MC::SetTiltLock, 0.0f);
		a_flip4->Message(MC::SetTiltLock, 0.0f);
		info_text_box->MessageField = 0x11;
		MissionControl(Code(MC::ControlMissionComplete), nullptr);
	}
	if (table->MultiballCount != 0 && s.LastBall == 0)
	{
		s.DrainState = 0;
		drain->Message(MC::Reset, 0.0f);
		return;
	}
	if (s.Mode11Done != 0)
	{
		s.Mode11Done = 0;
		coin_light_group->Message(MC::TLightResetAndTurnOff, 0.0f);
		special_treasure_light_group->Message(MC::TLightResetAndTurnOff, 0.0f);
	}
	if (info_text_box->MessageField == 0x10 || info_text_box->MessageField == 1)
		return;
	info_text_box->MessageField = 0x10;
	MissionControl(Code(MC::ControlMissionComplete), nullptr);
}

// 0x42ab90 Mode2IdleController
void Mode2IdleController(int code, TPinballComponent*)
{
	State& s = S();
	if (code == Code(MC::ControlMissionComplete))
	{
		s.NoHeldBall = 0;
		if (midi::active_track() != s.Music[0])
			midi::play_track(s.Music[0], false);
	}
	else if (code == Code(MC::ControlMissionStarted))
		info_text_box->Display(Str(3076), -1.0f, 0, false);
}

// 0x42abf0 Mode3LaunchController
void Mode3LaunchController(int code, TPinballComponent* caller)
{
	State& s = S();
	if (code == Code(MC::ControlCollision))
	{
		if (caller != s_oneway3)
			return;
		const int outcome = s_launcher1->MessageField;
		info_text_box->Clear();
		switch (outcome)
		{
		case 0:
			RandomMysteryAward(0.0f);
			SetModeIdle();
			return;
		case 1:
		case 2:
		case 8:
			AwardCoins(0.0f);
			SetModeIdle();
			return;
		case 3: StartMode5(); return;
		case 4: StartMode4(); return;
		case 5: StartMode8(); return;
		case 6: StartMode7(); return;
		case 7: StartMode6(); return;
		default: return;
		}
	}
	if (code == Code(MC::ControlMissionComplete))
	{
		s.NoHeldBall = 1;
		s.TargetsHit = 0;
		a_blocc->Message(MC::TBlockerSetFrame, -1.0f);
		a_bloce->Message(MC::TBlockerSetFrame, 0.0f);
		v_lite504->Message(MC::TLightResetAndTurnOff, 0.0f);
		v_lite505->Message(MC::TLightResetAndTurnOn, 0.0f);
		a_blocf->Message(MC::TBlockerSetFrame, 0.0f);
		a_blocg->Message(MC::TBlockerSetFrame, -1.0f);
		return;
	}
	if (code != Code(MC::ControlMissionStarted))
		return;
	// the wheel: next glyph string (wraps after 9), shown for 0.2 s; its expiry produces the next step
	int next = s_launcher1->MessageField + 1;
	if (next == 9)
		next = 0;
	s_launcher1->MessageField = next;
	info_text_box->Display(SkillGlyphs[next], 0.2f, 1, false);
}

// 0x42b820 Mode16BonusController
void Mode16BonusController(int code, TPinballComponent* caller)
{
	State& s = S();
	TPinballTable* table = control::TableG;
	// the original's entry switch; cases that fall to the step loop `break`, TFlipperExtend / TimerExpired re-enter it
	for (;;)
	{
		if (code == Code(MC::TFlipperExtend)) // BackdoorKey: skip the count
		{
			s.BonusSkip = 1;
			info_text_box->Clear();
			code = Code(MC::ControlMissionStarted);
			continue;
		}
		if (code == Code(MC::ControlTimerExpired))
		{
			if (mode_timer != caller)
				return;
			caller = nullptr;
			code = Code(MC::ControlMissionStarted);
			s.LastBall = 0;
			s.BonusStep = 8;
			continue;
		}
		break;
	}
	if (code == Code(MC::ControlMissionComplete))
	{
		s.BonusSkip = 0;
		s.BonusStep = 0;
		s.BonusTotal = 0;
		mode_timer->Message(MC::TTimerResetTimer, 30.0f);
		if (table->TiltLockFlag != 0 && s.LastBall != 1)
			s.BonusStep = 6;
		info_text_box->Clear();
		tabb42wave->Play(nullptr, "Mode16BonusController");
	}
	else if (code == Code(MC::ControlBallDrained))
	{
		if (s.BonusStep < 6)
			return;
	}
	else if (code != Code(MC::ControlMissionStarted))
		return;

	for (int guard = 0; guard < 16; ++guard) // the steps are 0..5 then the final one: at most 7 passes
	{
		const int step = s.BonusStep++;
		switch (step)
		{
		case 0:
			if (BonusStepText(s.TargetsHit * 10000, 3028, tabb84wave, "Mode16 targets"))
				return;
			break;
		case 1:
			if (BonusStepText(v_lite606->MessageField * 200000, 3029, tabb84wave, "Mode16 modes"))
				return;
			break;
		case 2:
			if (BonusStepText(v_lite607->MessageField * 250000, 3030, tabb84wave, "Mode16 map pieces"))
				return;
			break;
		case 3:
			if (BonusStepText(special_treasure_light_group->MessageField * 100000, 3031, tabb84wave, "Mode16 treasure"))
				return;
			break;
		case 4:
			if (BonusStepText(drain->MessageField, 3032, tabb84wave, "Mode16 held bonus"))
				return;
			break;
		case 5:
			if (!v_lite570->light_on())
				drain->MessageField = 0;
			else
			{
				v_lite570->Message(MC::TLightResetAndTurnOff, 0.0f);
				drain->MessageField = s.BonusTotal;
			}
			if (s.BonusTotal != 0)
			{
				SpecialAddScore(caller, s.BonusTotal);
				// the sprintf value is SpecialAddScore's result: AddScore with the multiplier forced to 1 returns the
				// score plus ScoreAdded (FT 0x419f10 return value, 0x423350 passes it through)
				const long added = static_cast<long>(s.BonusTotal) + table->ScoreAdded;
				char buf[64];
				std::snprintf(buf, sizeof buf, Str(3033), added);
				if (s.BonusSkip == 0)
				{
					tabb74wave->Play(nullptr, "Mode16 total");
					const float d = s.BatchFlag == 0 ? 2.0f : 0.25f;
					info_text_box->Display(buf, d / 2.0f + d, 0, false); // FT 0x42bf10: d / 2.0 + d
					return;
				}
			}
			break;
		default:
			if (s.LastBall != 0 && table->MultiballCount != 0)
				return;
			s.LastBall = 0;
			mode_timer->Message(MC::TTimerResetTimer, -1.0f);
			if (table->ExtraBalls == 0)
			{
				table->ChangeBallCount(table->BallCount - 1);
				if (table->CurrentPlayer - table->PlayerCount == -1 && table->BallCount == 0)
				{
					table->LightGroup->Message(MC::TLightResetAndTurnOff, 0.0f);
					info_text_box->MessageField = 1;
					MissionControl(Code(MC::ControlMissionComplete), nullptr);
					table->LightGroup->Message(MC::TLightFtResetOverride, 0.0f);
					return;
				}
				table->Message(MC::ClearTiltLock, 0.0f);
				drain->Message(MC::SetTiltLock, 0.0f);
				table->Message(MC::SwitchToNextPlayer, 0.0f);
				s_launcher1->Message(MC::PlungerStartFeedTimer, 0.96f);
			}
			else
			{
				table->ExtraBalls -= 1;
				const int player = table->CurrentPlayer;
				const int id = player == 0 ? 2011 : player == 1 ? 2012 : player == 2 ? 2013 : 2014;
				info_text_box->Display(Str(id), -1.0f, 0, false);
				table->Message(MC::ClearTiltLock, 0.0f);
				s_launcher1->Message(MC::PlungerStartFeedTimer, 0.96f);
			}
			info_text_box->MessageField = 3;
			MissionControl(Code(MC::ControlMissionComplete), nullptr);
			table->LightGroup->Message(MC::TLightFtResetOverride, 0.0f);
			return;
		}
	}
	Fatal("Mode16BonusController: step loop did not terminate");
}

// 0x42bfd0 Mode1GameOverController
void Mode1GameOverController(int code, TPinballComponent*)
{
	if (code != Code(MC::ControlMissionComplete))
		return;
	State& s = S();
	control::TableG->Message(MC::GameOver, 0.0f);
	if (midi::active_track() != s.Music[6])
		midi::play_track(s.Music[6], false);
	if (pb::chk_highscore())
	{
		tabb73wave->Play(nullptr, "Mode1GameOverController");
		control::TableG->LightGroup->Message(MC::TLightFlasherStartTimedThenStayOff, 3.0f);
		info_text_box->Display(Str(3027), -1.0f, 0, false);
	}
}

// 0x42d730 Mode17TiltController
void Mode17TiltController(int code, TPinballComponent*)
{
	State& s = S();
	TPinballTable* table = control::TableG;
	if (code == Code(MC::ControlMissionComplete))
	{
		s.Vestige = 0;
		a_kick2->ActiveFlag = 0;
		s_bwal610->ActiveFlag = 0;
		a_kick1->Message(MC::FtPartEnable, 0.0f);
		s_oneway12->ActiveFlag = 0;
		s_magnet1->Message(MC::FtPartDisable, 0.0f);
		s_magnet2->Message(MC::FtPartDisable, 0.0f);
		s_magnet3->Message(MC::FtPartDisable, 0.0f);
		s_magnet4->Message(MC::FtPartDisable, 0.0f);
		s_magnet5->Message(MC::FtPartDisable, 0.0f);
		s_magnet6->Message(MC::FtPartDisable, 0.0f);
		a_kout1->Message(MC::FtPartDisable, 0.0f);
		a_kout2->Message(MC::FtPartDisable, 0.0f);
		if (s.LockedBalls > 0)
		{
			table->MultiballCount += s.LockedBalls;
			s.LockedBalls = 0;
		}
	}
	else if (code == Code(MC::ControlBallDrained))
	{
		if (table->MultiballCount == 1)
			a_kick1->Message(MC::FtPartDisable, 0.0f);
	}
}
}
