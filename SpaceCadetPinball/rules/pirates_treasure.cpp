#include "pch.h"
#include "rules/pirates_rules.h"
#include "midi.h"
#include "table_text.h"
#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TLightSweeps.h"
#include "TPinballTable.h"
#include "TSink.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "TTripwire.h"
#include <cstdio>
#include <cstdlib>

using namespace pirates;

// Skulduggery rules: the Treasure X lights, the buried-treasure dig and its collection,
// modes 8 and 11. Ported from port_listing_pirates.c lines 745-764, 1215-1284, 1383-1507,
// 2147-2204, 6040-6138 and 6937-7101 (FT 0x423830, 0x424500, 0x424560, 0x424580, 0x424950, 0x424aa0, 0x424b00, 0x425ab0,
// 0x42b090, 0x42ce80, 0x42cfc0; PIRATES.EXE). All eleven functions were re-read in machine code (capstone):
// the listing drops the display-time argument of the chained text helpers, the count pointer of
// ShowTreasureBuried, the argument of AdvanceFathom and the mode_timer sends of Mode11; each is restored from the
// push sequence of the call.
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

int Code(MessageCode code) { return static_cast<int>(code); }

// FT type ids the controllers dispatch on (GetTypeId in the original).
const int TypeTimer = 1032;         // TTimer
const int TypeLightSweeps = 1038;   // TLightSweeps
const int TypeLightRollover = 1020; // TLightRollover (the mode 8 / mode 11 jackpot rollovers)

// The text display step of the dig helpers: a float + 2.0f (FT 0x4388e4), stored back as a float.
float Step(float time) { return static_cast<float>(2.0 + static_cast<double>(time)); }

const char* Text(int id)
{
	const char* text = table_text::Get(id);
	if (!text)
		Fatal("treasure: string resource unavailable from the EXE");
	return text;
}

// get_rc_string(id) + Display(box, text, time, 0, 0)
void Show(int id, float time)
{
	info_text_box->Display(Text(id), time, 0, false);
}

// crt_sprintf(buf, get_rc_string(id), value) + Display(box, buf, time, 0, 0); the formats of these ids hold one %ld.
void ShowValue(int id, long value, float time)
{
	char buf[64];
	std::snprintf(buf, sizeof buf, Text(id), value);
	info_text_box->Display(buf, time, 0, false);
}

// "<text> %ld" with the seconds left, 0.5 s (Mode 8 id 3053, Mode 11 id 3054).
void ShowSecondsLeft(int id)
{
	const int left = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
	if (left == 0)
		return;
	ShowValue(id, static_cast<long>(left / 1000), 0.5f);
}

// Mode 11 finale step glyphs: single font-1 character codes (FT 0x43ab16 + 2 * step, steps 1..8), not text.
const char* const FinaleGlyph[9] = {nullptr, "L", "M", "N", "O", "P", "Q", "R", "S"};

// The light-sweeps branch of ControlTimerExpired shared by both modes (identical machine code): with a held ball the sink is
// released, the mode timer is (re)started with the mode's 30.68 s and the mission-started text follows.
void SweepsEnded()
{
	if (S().NoHeldBall == 0)
		ReleaseHoldSink(0.0f);
	mode_timer->Message(MessageCode::TTimerResetTimer, 30.68f);
	MissionControl(Code(MessageCode::ControlMissionStarted), nullptr);
}

// The common head of the two modes' ControlTimerExpired case (FT 0x42b0b4 / 0x42cfe5).
void TimerExpired(TPinballComponent* caller)
{
	if (!caller)
		Fatal("treasure mode controller: ControlTimerExpired without a caller");
	const int type = caller->TypeId;
	if (type == TypeTimer)
	{
		if (caller == mode_timer)
			MissionControl(Code(MessageCode::ControlModeEnd), nullptr);
		return;
	}
	if (type == TypeLightSweeps)
		SweepsEnded();
}
}

namespace pirates {
// 0x423830. `time` is the chained display time; the result is time + 2.0.
float ShowText3040(float time)
{
	time = Step(time);
	Show(3040, time);
	return time;
}

// 0x424500
void TreasureXLightsOff()
{
	v_mlite44->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_mlite107->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_mlite106->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_mlite51->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_mlite110->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_mlite104->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x424560
void OrbitSinksInactive()
{
	a_sink9->ActiveFlag = 0;
	a_sink12->ActiveFlag = 0;
}

// 0x424580: arms both orbit sinks, flashes the next Treasure X (index 0..3, anything else wraps to the last one and resets
// the index to 0) and returns the light-sweeps pattern number of that X.
int LightNextTreasureX()
{
	State& s = S();
	a_sink9->ActiveFlag = 1;
	a_sink12->ActiveFlag = 1;
	const int index = s.TreasureXIndex;
	s.TreasureXIndex = index + 1;
	const unsigned which = static_cast<unsigned>(index); // unsigned compare against 3 (FT 0x424580): a negative index takes the default
	if (which == 0)
	{
		v_mlite110->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		left_orbit_light_group->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		return 5;
	}
	if (which == 1)
	{
		v_mlite107->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite525->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite520->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		return 2;
	}
	if (which == 2)
	{
		v_mlite104->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		right_orbit_light_group->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		return 6;
	}
	if (which == 3)
	{
		v_mlite106->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite526->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite521->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		return 3;
	}
	s.TreasureXIndex = 0;
	v_mlite44->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	v_lite524->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	v_lite519->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	return 1;
}

// 0x424950. The original's first argument is the address of the dig-state slot that keeps the lit-treasure count (0x435d18 /
// 0x435d1c); it receives the special-treasure light group's on-count, which also picks the text, the light and the
// string 3034..3039 (counts above 4 take the last one).
float ShowTreasureBuried(int* count, float time)
{
	if (!count)
		Fatal("ShowTreasureBuried: no count slot");
	const int lit = special_treasure_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	*count = lit;
	TLight* const lights[6] = {v_lite571, v_lite572, v_lite573, v_lite574, v_lite575, v_lite576};
	const unsigned which = static_cast<unsigned>(lit) <= 4u ? static_cast<unsigned>(lit) : 5u;
	const char* text = Text(3034 + static_cast<int>(which));
	lights[which]->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
	time = Step(time);
	info_text_box->Display(text, time, 0, false);
	tabb84wave->Play(nullptr, "ShowTreasureBuried");
	return time;
}

// 0x424aa0: the bonus a buried "Bonus" pays.
int ComputeBuriedBonus()
{
	const int base = (special_treasure_light_group->MessageField * 2 + v_lite607->MessageField) * 25
	                 + v_lite606->MessageField * 20 + S().TargetsHit;
	return fathom_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f) * base * 10000;
}

// 0x424b00: one dig step. DigState = 0x435d10 (step), d14 (coins), d18 / d1c (lit-treasure counts), d20 (bonus), d24 (jackpot),
// d28 (extra ball). Steps 0..2 and everything past 7 bury coins, 3 and 4 a special treasure, 5 a bonus, 6 the jackpot, 7 an
// extra ball.
float DigTreasure(float time)
{
	State& s = S();
	const int step = s.DigState[0];
	s.DigState[0] = step + 1;
	switch (static_cast<unsigned>(step) <= 7u ? step : -1)
	{
	case 3:
		return ShowTreasureBuried(&s.DigState[2], time);
	case 4:
		return ShowTreasureBuried(&s.DigState[3], time);
	case 5:
		time = Step(time);
		Show(3041, time);
		s.DigState[4] = ComputeBuriedBonus();
		return time;
	case 6:
		time = Step(time);
		Show(3042, time);
		s.DigState[5] = control::TableG->JackpotScore;
		return time;
	case 7:
		time = Step(time);
		Show(3043, time);
		s.DigState[6] = 1;
		return time;
	default: // 0, 1, 2 and the steps past 7
		time = ShowText3040(time);
		s.DigState[1] = s.DigState[1] + 1;
		return time;
	}
}

// 0x425ab0: pays the whole buried batch (coins, the two treasures, bonus, jackpot, extra ball), chaining the text display
// time through the awards. ORIGINAL BUG KEPT: the second treasure is awarded with the index slot
// 0x435d18 again (FT 0x425b29 reads slot 0x435d18), which the first award has just reset to -1, so it is awarded with -1 or,
// when only the second slot was used, with the first slot's -1 too. The extra-ball award's result is discarded (0x425c28).
float CollectBuriedTreasure(float time)
{
	State& s = S();
	s.BatchFlag = 1;
	s.DigState[0] = 0;
	while (s.DigState[1] > 0)
	{
		time = AwardCoins(time);
		s.DigState[1] = s.DigState[1] - 1;
	}
	s.DigState[1] = 1;
	if (s.DigState[2] != -1)
	{
		time = AwardSpecialTreasure(s.DigState[2], time);
		s.DigState[2] = -1;
	}
	if (s.DigState[3] != -1)
	{
		time = AwardSpecialTreasure(s.DigState[2], time); // the original's bug: slot d18, not d1c
		s.DigState[3] = -1;
	}
	if (s.DigState[4] != 0)
	{
		const int bonus = s.DigState[4];
		SpecialAddScore(nullptr, bonus);
		// the printed value is SpecialAddScore's result: AddScore with the multiplier forced to 1 (FT 0x423350)
		const long added = static_cast<long>(bonus) + control::TableG->ScoreAdded;
		time = Step(time);
		ShowValue(3057, added, time);
		s.DigState[4] = 0;
	}
	if (s.DigState[5] != 0)
	{
		const int jackpot = s.DigState[5];
		SpecialAddScore(nullptr, jackpot);
		const long added = static_cast<long>(jackpot) + control::TableG->ScoreAdded;
		time = Step(time);
		ShowValue(3058, added, time);
		tabb85wave->Play(nullptr, "CollectBuriedTreasure");
		s.DigState[5] = 0;
	}
	if (s.DigState[6] != 0)
	{
		AwardExtraBall(time);
		s.DigState[6] = 0;
	}
	s.BatchFlag = 0;
	return time;
}

// 0x42b090: mode 8.
void Mode8Controller(int code, TPinballComponent* caller)
{
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		TimerExpired(caller);
		return;
	case MessageCode::ControlCollision:
		if (!caller)
			Fatal("Mode8Controller: ControlCollision without a caller");
		if (caller->TypeId == TypeLightRollover)
			AddJackpot(20000);
		return;
	case MessageCode::ControlMissionComplete:
	{
		ModeStartCleanup();
		a_blocc->Message(MessageCode::TBlockerSetFrame, -1.0f);
		a_blocf->Message(MessageCode::TBlockerSetFrame, -1.0f);
		a_blocg->Message(MessageCode::TBlockerSetFrame, -1.0f);
		ScoreValue(nullptr, 300000);
		TreasureXLightsOff();
		s_trip3->ActiveFlag = 0;
		s_trip8->ActiveFlag = 0;
		s_trip3->MessageField = left_orbit_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		s_trip8->MessageField = right_orbit_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		left_orbit_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		right_orbit_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		left_orbit_combo_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		right_orbit_combo_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite520->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite521->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		const int pattern = LightNextTreasureX();
		light_sweeps->Message(MessageCode::TLightSweepsStart, static_cast<float>(pattern));
		midi::play_track(S().Music[2], true); // 0x43ab88
		tabb59wave->Play(nullptr, "Mode8Controller");
		Show(3061, -1.0f);
		return;
	}
	case MessageCode::ControlMissionStarted:
		ShowSecondsLeft(3053);
		return;
	case MessageCode::ControlModeEnd:
		TreasureXLightsOff();
		OrbitSinksInactive();
		ModeEndRestoreLights();
		v_lite606->MessageField = v_lite606->MessageField + 1;
		a_bloce->Message(MessageCode::TBlockerSetFrame, 0.0f);
		v_lite504->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite505->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		v_lite519->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		s_trip3->ActiveFlag = 1;
		s_trip8->ActiveFlag = 1;
		left_orbit_light_group->Message(MessageCode::TLightTurnOff, 0.0f);
		right_orbit_light_group->Message(MessageCode::TLightTurnOff, 0.0f);
		left_orbit_light_group->Message(MessageCode::TLightGroupToggleSplitIndex, static_cast<float>(s_trip3->MessageField));
		right_orbit_light_group->Message(MessageCode::TLightGroupToggleSplitIndex, static_cast<float>(s_trip8->MessageField));
		return;
	case MessageCode::ControlBallDrained:
		mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
		MissionControl(Code(MessageCode::ControlModeEnd), nullptr);
		return;
	default:
		return;
	}
}

// 0x42ce80
void Mode11Setup()
{
	TreasureXLightsOff();
	a_sink9->ActiveFlag = 1;
	a_sink12->ActiveFlag = 1;
	s_trip3->ActiveFlag = 0;
	s_trip8->ActiveFlag = 0;
	v_lite568->MessageField = v_lite568->light_on() ? 1 : 0;
	left_orbit_light_group->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
	right_orbit_light_group->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
	v_mlite51->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	v_lite4->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	v_lite5->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	v_lite6->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	v_lite7->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	v_lite8->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	v_lite568->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	v_lite567->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	light_sweeps->Message(MessageCode::TLightSweepsStart, 4.0f);
	v_lite520->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite521->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x42cfc0: mode 11. MissionStarted runs the finale: the shared counter 0x435cf8 (ModeCounter) is the step
// (0: the time-left text; 1: stops the mode timer, plays the finale sound and shows glyph 1; 2..8: glyph n; 9: the payout;
// anything else: ends the mode and starts mode 12). Other rules advance the counter from 0 to 1.
void Mode11Controller(int code, TPinballComponent* caller)
{
	State& s = S();
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		TimerExpired(caller);
		return;
	case MessageCode::ControlCollision:
		if (!caller)
			Fatal("Mode11Controller: ControlCollision without a caller");
		if (caller->TypeId == TypeLightRollover)
			AddJackpot(50000);
		return;
	case MessageCode::ControlMissionComplete:
		ModeStartCleanup();
		ScoreValue(nullptr, 1000000);
		Mode11Setup();
		midi::play_track(s.Music[2], true); // 0x43ab88
		tabb99wave->Play(nullptr, "Mode11Controller");
		Show(3066, -1.0f);
		return;
	case MessageCode::ControlMissionStarted:
	{
		const unsigned step = static_cast<unsigned>(s.ModeCounter);
		if (step == 0)
		{
			ShowSecondsLeft(3054);
			return;
		}
		if (step <= 8)
		{
			if (step == 1)
			{
				mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
				tabb28wave->Play(nullptr, "Mode11Controller");
			}
			info_text_box->Display(FinaleGlyph[step], 0.25f, 1, false);
			s.ModeCounter = s.ModeCounter + 1;
			return;
		}
		if (step == 9)
		{
			const int coins = coin_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
			const int treasures = special_treasure_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
			const int pieces = map_piece_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
			const int modesPlayed = v_lite606->MessageField;
			AdvanceFathom(-1.0f);
			AwardFullCrew(-1.0f);
			AwardJackpot(0.0f);
			const int added = ScoreValue(nullptr, coins * 500000 + treasures * 500000 + pieces * 1000000 + modesPlayed * 1000000);
			ShowValue(3003, static_cast<long>(added), 4.0f);
			s.ModeCounter = s.ModeCounter + 1;
			return;
		}
		MissionControl(Code(MessageCode::ControlModeEnd), nullptr);
		ReleaseHoldSink(0.0f);
		StartMode12();
		return;
	}
	case MessageCode::ControlModeEnd:
		TreasureXLightsOff();
		OrbitSinksInactive();
		ModeEndRestoreLights();
		v_lite606->MessageField = v_lite606->MessageField + 1;
		a_bloce->Message(MessageCode::TBlockerSetFrame, 0.0f);
		v_lite504->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite505->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		s_trip3->ActiveFlag = 1;
		s_trip8->ActiveFlag = 1;
		left_orbit_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
		right_orbit_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
		left_orbit_combo_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		right_orbit_combo_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		// adc: the code is TLightResetAndTurnOff (0x14) when the saved field is 0, TLightResetAndTurnOn (0x13) otherwise
		v_lite568->Message(v_lite568->MessageField == 0 ? MessageCode::TLightResetAndTurnOff : MessageCode::TLightResetAndTurnOn,
		                   0.0f);
		v_lite567->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		map_piece_light_group->Message(MessageCode::TLightTurnOff, 0.0f);
		map_piece_light_group->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite552->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite553->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite554->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite555->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite556->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite557->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		s.Mode11Done = 1;
		return;
	case MessageCode::ControlBallDrained:
		mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
		MissionControl(Code(MessageCode::ControlModeEnd), nullptr);
		return;
	default:
		return;
	}
}
}
