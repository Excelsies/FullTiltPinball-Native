#include "pch.h"
#include "rules/dragon_rules.h"
#include "TDrain.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "table_text.h"
#include <cstdio>

// Dragon's Keep rules: the end-of-ball bonus count, mode 11 (port of DRAGON.EXE 0x42b7c0
// Mode11BonusController). Rule globals used (DragonState): BonusTotal 0x434d00, TargetsHit 0x434d08, BonusSkip 0x434d0c,
// BonusStep 0x434d10, Spells 0x434d24, LastBall 0x439b1c. One step per call, each earned step shows its text for 2.0 s and
// returns; the table's text-box expiry (ControlTimerExpired on info_text_box) resumes the count at the next step.
namespace dragon {
namespace {
// TTextBox::Display(info_text_box, get_rc_string(id), time, 0, 0). The text comes from the user's EXE at run time;
// when it is unavailable nothing is shown (and the reader has logged why), never a substitute text.
void ShowText(int id, float time)
{
	const char* text = table_text::Get(id);
	if (!text)
	{
		std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), bonus shows nothing\n", id);
		return;
	}
	info_text_box->Display(text, time, 0, false);
}

// crt_sprintf(buf[64], get_rc_string(id), value), then Display(buf, 2.0). The format is the game's own data.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
void ShowValueText(int id, int value)
{
	const char* format = table_text::Get(id);
	if (!format)
	{
		std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), bonus shows nothing\n", id);
		return;
	}
	char buffer[64];
	std::snprintf(buffer, sizeof buffer, format, value);
	info_text_box->Display(buffer, 2.0f, 0, false);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// The tail of every earned step: the text (unless the skip flag is set) and the tick sound; true = the count pauses here.
bool Announce(DragonState& st, int textId, int value, TSound* sound)
{
	if (st.BonusSkip != 0)
		return false;
	ShowValueText(textId, value);
	sound->Play(nullptr, "Mode11BonusController");
	return true;
}

// Step 8 and later: the ball is over; hand the table to the next ball / player or end the game.
void EndOfBonus(DragonState& st)
{
	TPinballTable* table = control::TableG;
	if (st.LastBall != 0 && table->MultiballCount != 0)
		return;
	st.LastBall = 0;
	mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
	if (table->ExtraBalls == 0)
	{
		table->ChangeBallCount(table->BallCount - 1);
		if (table->CurrentPlayer - table->PlayerCount == -1 && table->BallCount == 0)
		{
			table->LightGroup->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			info_text_box->MessageField = 0;
			MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
		}
		else
		{
			table->Message(MessageCode::ClearTiltLock, 0.0f);
			drain->Message(MessageCode::SetTiltLock, 0.0f);
			table->Message(MessageCode::SwitchToNextPlayer, 0.0f);
			s_plunger1->Message(MessageCode::PlungerStartFeedTimer, 0.96f);
		}
	}
	else
	{
		table->ExtraBalls = table->ExtraBalls - 1;
		// per-player text ids 2011..2014 (0x7db..0x7de)
		const int player = table->CurrentPlayer;
		ShowText(player == 0 ? 2011 : player == 1 ? 2012 : player == 2 ? 2013 : 2014, -1.0f);
		table->Message(MessageCode::ClearTiltLock, 0.0f);
		s_plunger1->Message(MessageCode::PlungerStartFeedTimer, 0.96f);
	}
	table->LightGroup->Message(MessageCode::TLightFtResetOverride, 0.0f);
}
}

void Mode11BonusController(int code, TPinballComponent* caller)
{
	DragonState& st = S();
	TPinballTable* table = control::TableG;
	if (code == static_cast<int>(MessageCode::TFlipperExtend))
	{
		// a flipper press skips the texts: the count still runs, silently, to the end
		st.BonusSkip = 1;
		info_text_box->Clear();
		code = static_cast<int>(MessageCode::ControlMissionStarted);
	}
	if (code == static_cast<int>(MessageCode::ControlTimerExpired))
	{
		// the 30 s watchdog (mode_timer): finish the count with no further texts of its own
		if (caller != mode_timer)
			return;
		caller = nullptr;
		st.LastBall = 0;
		st.BonusStep = 8;
		code = static_cast<int>(MessageCode::ControlMissionStarted);
	}
	if (code == static_cast<int>(MessageCode::ControlMissionComplete))
	{
		st.BonusSkip = 0;
		st.BonusStep = 0;
		st.BonusTotal = 0;
		mode_timer->Message(MessageCode::TTimerResetTimer, 30.0f);
		if (table->TiltLockFlag != 0 && st.LastBall != 1)
			st.BonusStep = 8;
		table->LightGroup->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
		info_text_box->Clear();
		tabc44wave->Play(nullptr, "Mode11BonusController");
	}
	else if (code == static_cast<int>(MessageCode::ControlBallDrained))
	{
		if (st.BonusStep < 8)
			return;
	}
	else if (code != static_cast<int>(MessageCode::ControlMissionStarted))
		return;

	// Each pass takes one step; every case but the last returns or falls to the next step, and BonusStep only grows,
	// so the loop ends at the step 8 branch at the latest.
	for (;;)
	{
		const int step = st.BonusStep++;
		switch (step)
		{
		case 0:
			if (st.TargetsHit != 0)
			{
				st.BonusTotal += st.TargetsHit * 2000;
				if (Announce(st, 3012, st.TargetsHit * 2000, tabc06wave))
					return;
			}
			break;
		case 1:
		{
			const int quests = v_lite28->MessageField;
			CancelQuestSelection();
			if (quests != 0)
			{
				st.BonusTotal += quests * 100000;
				if (Announce(st, 3013, quests * 100000, tabc06wave))
					return;
			}
			break;
		}
		case 2:
		{
			int lit = 0;
			if (v_lite37->light_on())
				lit = 1;
			if (v_lite38->light_on())
				lit++;
			if (v_lite39->light_on())
				lit++;
			if (lit != 0)
			{
				st.BonusTotal += lit * 50000;
				if (Announce(st, 3014, lit * 50000, tabc06wave)) // value read from the machine code at 0x42ba4b
					return;
			}
			break;
		}
		case 3:
			if (st.Spells != 0)
			{
				st.BonusTotal += st.Spells * 75000;
				if (Announce(st, 3015, st.Spells * 75000, tabc06wave))
					return;
			}
			break;
		case 4:
		{
			const int eggs = dragon_egg_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
			if (eggs != 0)
			{
				st.BonusTotal += eggs * 150000;
				if (Announce(st, 3069, eggs * 150000, tabc06wave))
					return;
			}
			break;
		}
		case 5:
			if (table->ScoreMultiplier > 1)
			{
				st.BonusTotal += table->ScoreMultiplier * 75000;
				if (Announce(st, 3070, table->ScoreMultiplier * 75000, tabc06wave))
					return;
			}
			break;
		case 6:
			if (drain->MessageField != 0)
			{
				st.BonusTotal += drain->MessageField;
				if (Announce(st, 3016, drain->MessageField, tabc06wave))
					return;
			}
			break;
		case 7:
			if (!v_lite31->light_on())
				drain->MessageField = 0;
			else
			{
				v_lite31->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
				drain->MessageField = st.BonusTotal;
			}
			if (st.BonusTotal != 0)
			{
				// the original prints SpecialAddScore's result: the AddScore return, which with the multiplier forced to 1
				// is ScoreAdded + score (SpecialAddScore returns nothing here)
				const int before = table->ScoreAdded;
				SpecialAddScore(caller, st.BonusTotal);
				if (Announce(st, 3017, before + st.BonusTotal, tabc13wave))
					return;
			}
			break;
		default:
			EndOfBonus(st);
			return;
		}
	}
}
}
