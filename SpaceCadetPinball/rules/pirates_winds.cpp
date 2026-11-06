#include "pch.h"
#include "rules/pirates_rules.h"
#include "table_text.h"
#include <cstdio>
#include <cstdlib>
#include "TKickback.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TWall.h"

using namespace pirates;

// Skulduggery rules: the mode 5 wind light helpers, the five WINDS lane parts, the four
// wind rollovers and the three island lane rollovers. Ported from port_listing_pirates.c (FT 0x425080, 0x425110, 0x425230,
// 0x429630, 0x4296d0, 0x429790, 0x429840, 0x4298f0, 0x429ac0, 0x429c30, 0x429da0, 0x429fa0, 0x42a060, 0x42a120) and re-read
// against the machine code: every `unaff_retaddr` / `piStack` ScorePart receiver is the control
// function's second argument `caller`; the listing's second ScorePart / SpecialAddScore line before a crt_sprintf is the same
// call, not a second one.
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

const char* Rc(int id)
{
	const char* text = table_text::Get(id);
	if (!text)
		Fatal("winds: string resource unavailable");
	return text;
}

// TTextBox::Display(box, text, time, font, lowPriority = 0) at FT 0x41de40
void Show(const char* text, float time, int font)
{
	info_text_box->Display(text, time, font, false);
}

// The font-1 glyph strings of the original's .data (FT 0x43aac0, 0x43aac4, 0x43aac8, 0x43aacc, 0x43aad0): the digit glyphs
// '5' (WINDS tripwire), '6' and '7' (rollovers 5 and 1), '8' (south wind), '9' (rollover 3). Glyph codes, not text.
const char* const TripwireGlyph = "5";
const char* const Roll5Glyph = "6";
const char* const Roll1Glyph = "7";
const char* const SouthWindGlyph = "8";
const char* const Roll3Glyph = "9";

// The mode 5 wind score table at FT 0x43d0e0 (four 32-bit entries, indexed by the per-mode counter).
const int Mode5WindScore[4] = {125000, 250000, 500000, 1000000};

const int WindSpecialScore = 50000; // 0xc350: the mode-5 score of a wind rollover whose wind light is not flashing

int TableScore()
{
	TPinballTable* table = control::TableG;
	const long long total = static_cast<long long>(table->CurScoreE9) * 1000000000LL + table->CurScore;
	return static_cast<int>(total);
}

// SpecialAddScore's result in the original is TPinballTable::AddScore's return (FT 0x4233c5 leaves it in eax, 0x423350 ends
// there); FtRules::SpecialAddScore returns nothing, so the amount is the change of the table score (as in pirates_awards.cpp).
int SpecialAddScoreAmount(TPinballComponent* comp, int score)
{
	const int before = TableScore();
	pirates::SpecialAddScore(comp, score);
	return TableScore() - before;
}

void ShowWithValue(int stringId, int value, float time)
{
	char text[64]; // the original's 64-byte buffer
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
	// the format is the EXE's own string, not a literal
	std::snprintf(text, sizeof text, Rc(stringId), static_cast<long>(value));
#pragma GCC diagnostic pop
	Show(text, time, 0);
}

// The shared body of the four wind rollovers: the number of lit lights of the wind's group picks the score index; in mode 2
// the next light(s) of the wind flash for 2.0 s (the second light only when exactly one is lit already, the whole group when
// two or more are). Returns the ScorePart index.
int WindIndex(TLightGroup* group, TLight* first, TLight* second)
{
	const int lit = group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	if (lit == 0)
	{
		if (info_text_box->MessageField == 2)
			first->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		return 0;
	}
	if (lit == 1)
	{
		if (info_text_box->MessageField == 2)
		{
			first->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			second->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		}
		return 1;
	}
	if (info_text_box->MessageField == 2)
		group->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
	return 2;
}

// Mode 5 on a wind rollover: a flashing wind light is the current target (Mode5WindHit), any other hit scores a
// flat 50,000 without the multiplier. Returns true when the mode-5 branch handled the hit.
bool Mode5Hit(TLight* windLight, TPinballComponent* caller)
{
	if (windLight->FlasherOnFlag)
	{
		Mode5WindHit();
		return true;
	}
	SpecialAddScore(caller, WindSpecialScore);
	tabb12wave->Play(nullptr, "WindRollover");
	return true;
}

// One of the three WINDS rollovers a_roll5 / a_roll1 (FT 0x429790, 0x429840): they differ in the lane light, the glyph and
// nothing else. The lane light flashes in mode 2; when it completes the five WINDS lights the map piece is awarded (and the
// text and sound of the rollover are skipped); the kickback is armed in every case.
void WindsKickbackRollover(TLight* laneLight, const char* glyph, TPinballComponent* caller)
{
	if (info_text_box->MessageField == 2)
	{
		laneLight->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		if (CheckWindsComplete(0.0f) != 0.0f)
		{
			// the map piece took the display: no glyph, no sound
			v_lite544->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			a_kick2->ActiveFlag = 1;
			s_bwal610->ActiveFlag = 1;
			ScorePart(caller, 0);
			return;
		}
		Show(glyph, 2.0f, 1);
	}
	tabb44wave->Play(nullptr, "WindsRollover");
	v_lite544->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	a_kick2->ActiveFlag = 1;
	s_bwal610->ActiveFlag = 1;
	ScorePart(caller, 0);
}

// One of the three island lane rollovers (FT 0x429fa0, 0x42a060, 0x42a120): top lane light, bottom lane light.
void IslandLaneRollover(TLight* top, TLight* bottom, TPinballComponent* caller)
{
	if (!top->LightOnFlag)
	{
		tabb44wave->Play(nullptr, "IslandLaneRollover");
		ScorePart(caller, 0);
		top->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		return;
	}
	if (!bottom->LightOnFlag)
	{
		bottom->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		IslandLaneBank(caller);
		return;
	}
	top->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	bottom->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	tabb44wave->Play(nullptr, "IslandLaneRollover");
	ScorePart(caller, 0);
}
}

namespace pirates {

// 0x425080. The first call is ScoreValue(0, 0) in the machine code (0x425080-0x425084): it only replaces the last scorer.
void WindLightsOff()
{
	ScoreValue(nullptr, 0);
	left_island_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	right_wind_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	left_orbit_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	right_orbit_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	south_wind_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	east_wind_1_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	east_wind_2_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	west_wind_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x425110. param_1 == 0 (low byte): the wind's light group is switched on steady (code 0x13), else it flashes (code 7), both
// with -1.0. The wind index 0..7 picks the group (jump table 0x425204); an index outside 0..7 (unsigned compare) resets it to
// 0 and uses the index-0 group.
void SetMode5WindLight(int param_1)
{
	const MessageCode code = (static_cast<char>(param_1) == 0) ? MessageCode::TLightResetAndTurnOn : MessageCode::TLightFlasherStartTimed;
	int& index = S().TreasureXIndex;
	TLightGroup* group;
	switch (static_cast<unsigned>(index) > 7u ? 8u : static_cast<unsigned>(index))
	{
	case 1: group = right_orbit_light_group; break;
	case 2: group = left_island_light_group; break;
	case 3: group = right_wind_light_group; break;
	case 4: group = south_wind_light_group; break;
	case 5: group = west_wind_light_group; break;
	case 6: group = east_wind_2_light_group; break;
	case 7: group = east_wind_1_light_group; break;
	case 8: index = 0; group = left_orbit_light_group; break;
	default: group = left_orbit_light_group; break;
	}
	group->Message(code, -1.0f);
}

// 0x425230
void Mode5WindHit()
{
	SetMode5WindLight(0);
	S().TreasureXIndex++;
	SetMode5WindLight(1);
	if (S().ModeCounter < 4)
	{
		const int amount = SpecialAddScoreAmount(nullptr, Mode5WindScore[S().ModeCounter]);
		ShowWithValue(3003, amount, 2.0f);
		tabb12wave->Play(nullptr, "Mode5WindHit");
		S().ModeCounter++;
		return;
	}
	AwardJackpot(0.0f);
	S().ModeCounter = 0;
}

// 0x429630 (s_trip11)
void WindsTripwireControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField == 2)
	{
		v_lite533->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		// a completed WINDS set awards the map piece (non-zero display time) and ends the call
		if (CheckWindsComplete(0.0f) != 0.0f)
			return;
		ScorePart(caller, 0);
		Show(TripwireGlyph, 2.0f, 1);
		tabb43wave->Play(nullptr, "WindsTripwire");
		return;
	}
	ScorePart(caller, 0);
	tabb43wave->Play(nullptr, "WindsTripwire");
}

// 0x4296d0 (a_roll3). In mode 2 the lane light flashes and a completed set awards the map piece; a lit mystery award (v_lite529) is then
// collected; only when neither took the display do the sound, the glyph and the score happen.
void WindsRolloverControl1(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	float time = 0.0f;
	if (info_text_box->MessageField == 2)
	{
		v_lite537->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		time = CheckWindsComplete(0.0f);
	}
	if (time == 0.0f)
		time = CollectMysteryAward(0.0f);
	if (time == 0.0f)
	{
		tabb43wave->Play(nullptr, "WindsRollover");
		Show(Roll3Glyph, 2.0f, 1);
		ScorePart(caller, 0);
	}
}

// 0x429790 (a_roll5)
void WindsRolloverControl2(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	WindsKickbackRollover(v_lite534, Roll5Glyph, caller);
}

// 0x429840 (a_roll1)
void WindsRolloverControl3(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	WindsKickbackRollover(v_lite535, Roll1Glyph, caller);
}

// 0x4298f0 (a_roll2). The local at [esp] of the machine code holds CheckWindsComplete's result (mode 2) or 0; the final text
// is shown 2.0 s after it, i.e. after a map piece text (the listing's `fVar5 = 5.7e-44` initialiser is a decompiler artefact).
void SouthWindRolloverControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	float time = 0.0f;
	const int mode = info_text_box->MessageField;
	if (mode == 2)
	{
		v_lite536->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		time = CheckWindsComplete(0.0f);
		if (time == 0.0f)
		{
			Show(SouthWindGlyph, 2.0f, 1);
			tabb12wave->Play(nullptr, "SouthWindRollover");
		}
	}
	else if (mode == 5)
	{
		Mode5Hit(v_lite622, caller);
		return;
	}
	const int index = WindIndex(south_wind_light_group, v_lite622, v_lite621);
	const int amount = ScorePart(caller, index);
	ShowWithValue(3010, amount, static_cast<float>(static_cast<double>(time) + 2.0));
}

// 0x429ac0 (a_roll9)
void EastWindRolloverControl1(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	const int mode = info_text_box->MessageField;
	if (mode == 5)
	{
		Mode5Hit(v_lite616, caller);
		return;
	}
	if (mode == 8)
	{
		DigTreasure(0.0f);
		return;
	}
	tabb12wave->Play(nullptr, "EastWindRollover");
	const int index = WindIndex(east_wind_1_light_group, v_lite616, v_lite615);
	ShowWithValue(3011, ScorePart(caller, index), 2.0f);
}

// 0x429c30 (a_roll8)
void EastWindRolloverControl2(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	const int mode = info_text_box->MessageField;
	if (mode == 5)
	{
		Mode5Hit(v_lite619, caller);
		return;
	}
	if (mode == 8)
	{
		DigTreasure(0.0f);
		return;
	}
	tabb12wave->Play(nullptr, "EastWindRollover");
	const int index = WindIndex(east_wind_2_light_group, v_lite619, v_lite618);
	ShowWithValue(3011, ScorePart(caller, index), 2.0f);
}

// 0x429da0 (a_roll6)
void WestWindRolloverControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField == 5)
	{
		Mode5Hit(v_lite623, caller);
		return;
	}
	tabb12wave->Play(nullptr, "WestWindRollover");
	const int index = WindIndex(west_wind_light_group, v_lite623, v_lite624);
	ShowWithValue(3012, ScorePart(caller, index), 2.0f);
}

// 0x429fa0 (a_roll12)
void IslandLaneRolloverControl1(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		IslandLaneRollover(v_lite538, v_lite541, caller);
}

// 0x42a060 (a_roll11)
void IslandLaneRolloverControl2(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		IslandLaneRollover(v_lite539, v_lite542, caller);
}

// 0x42a120 (a_roll10)
void IslandLaneRolloverControl3(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		IslandLaneRollover(v_lite540, v_lite543, caller);
}
}
