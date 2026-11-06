#include "pch.h"
#include "rules/pirates_rules.h"
#include "table_text.h"
#include <cstdio>
#include <cstdlib>
#include "TBumper.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"

using namespace pirates;

// Skulduggery rules: map pieces, fathom, the special_treasure lights, the full-crew award, the
// mystery award light (v_lite529), the island lane bank and the ship ramp exit. Ported from port_listing_pirates.c (FT 0x423a30, 0x423a80,
// 0x423aa0, 0x424290, 0x424350, 0x4243d0, 0x424780, 0x424930, 0x425000, 0x425c50, 0x425d10, 0x429140, 0x429ef0) and
// re-read against the machine code: the listing drops the display-time argument and several return values.
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
		Fatal("awards: string resource unavailable");
	return text;
}

// TTextBox::Display(box, text, time, font, lowPriority = 0) at FT 0x41de40
void Show(const char* text, float time, int font)
{
	info_text_box->Display(text, time, font, false);
}

void PlaySound(TSound* wave, const char* info)
{
	wave->Play(nullptr, info);
}

// Font-1 glyph strings of the original's .data: fathom ladder at 0x43aad8 (4-byte slots, indexed by the fathom light group's
// on-count) followed by the map piece glyph at 0x43aaf4 (and the coin glyph at 0x43aaf8, which an on-count of 8 would read).
const char* const FathomGlyphs[] = {":", ":=<", ":><", ":?<", ":@<", ":A<", ":B<", "C", "D"};
const char* const MapPieceGlyph = "C";

const char* FathomGlyph(int onCount)
{
	if (onCount < 0 || onCount >= static_cast<int>(sizeof FathomGlyphs / sizeof FathomGlyphs[0]))
		Fatal("fathom light group on-count outside the glyph table");
	return FathomGlyphs[onCount];
}

// The original's ScorePart returns TPinballTable::AddScore's result (FT 0x419f10: ScoreMultiplier * score + ScoreAdded),
// which the ship ramp text prints. Here the amount is taken as the change of the table score across the call.
int ScorePartAmount(TPinballComponent* comp, int index)
{
	TPinballTable* table = control::TableG;
	const long long before = static_cast<long long>(table->CurScoreE9) * 1000000000LL + table->CurScore;
	pirates::ScorePart(comp, index);
	const long long after = static_cast<long long>(table->CurScoreE9) * 1000000000LL + table->CurScore;
	return static_cast<int>(after - before);
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
}

namespace pirates {
// 0x423a30
float LightMysteryAward(float time)
{
	time += 2.0f;
	Show(Rc(3059), time, 0);
	v_lite529->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	return time;
}

// 0x423a80
void ClearMysteryAward()
{
	v_lite529->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
}

// 0x423aa0
float AwardMapPiece(float time)
{
	map_piece_light_group->Message(MessageCode::TLightGroupResetAndTurnOn, 1.0f);
	time += 2.0f;
	v_lite607->MessageField++;
	Show(MapPieceGlyph, time, 1);
	time += 2.0f;
	Show(Rc(3015), time, 0);
	ScoreValue(nullptr, 1000000);
	PlaySound(tabb83wave, "AwardMapPiece");
	const int lit = map_piece_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	const int total = map_piece_light_group->Message(MessageCode::TLightGroupGetLightCount, 0.0f);
	if (lit == total && info_text_box->MessageField == 2)
		SelectNextMode();
	return time;
}

// 0x424290
float CheckWindsComplete(float time)
{
	if (v_lite533->light_on() && v_lite534->light_on() && v_lite535->light_on() && v_lite536->light_on()
		&& v_lite537->light_on())
	{
		lane_winds_light_group->Message(MessageCode::TLightTurnOff, 0.0f);
		lane_winds_light_group->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		time = AwardMapPiece(time);
	}
	return time;
}

// 0x424350
float AdvanceFathom(float time)
{
	fathom_light_group->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	const int count = fathom_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	if (time >= 0.0f)
	{
		time += 2.0f;
		Show(FathomGlyph(count), time, 1);
	}
	control::TableG->ScoreMultiplier = count + 1;
	return time;
}

// 0x4243d0
float AdvanceFathomIsland(float time)
{
	fathom_light_group->Message(MessageCode::TLightGroupResetAndTurnOn, 1.0f);
	const int count = fathom_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	time += 2.0f;
	Show(FathomGlyph(count), time, 1);
	control::TableG->ScoreMultiplier = count + 1;
	return time;
}

// 0x424780. Returns the shifted display time (FT 0x4248ef stores it into the time argument slot, 0x424907 returns that slot), so
// chained calls (buried-treasure batch) advance in 0.25 s steps.
float AwardSpecialTreasure(int index, float time)
{
	special_treasure_light_group->MessageField++;
	TLight* light;
	int stringId, score;
	switch (static_cast<unsigned>(index) > 4u ? 5 : index)
	{
	case 0: stringId = 3019; light = v_lite571; score = 100000; break;
	case 1: stringId = 3020; light = v_lite572; score = 250000; break;
	case 2: stringId = 3021; light = v_lite573; score = 500000; break;
	case 3: stringId = 3022; light = v_lite574; score = 750000; break;
	case 4: stringId = 3023; light = v_lite575; score = 1000000; break;
	default: stringId = 3024; light = v_lite576; score = 1500000; break;
	}
	const char* text = Rc(stringId);
	light->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	ScoreValue(nullptr, score);
	const float shown = static_cast<float>(static_cast<double>(time) + (S().BatchFlag ? 0.25 : 2.0));
	Show(text, shown, 0);
	PlaySound(tabb84wave, "AwardSpecialTreasure");
	return shown;
}

// 0x424930
float AwardNextSpecialTreasure(float time)
{
	return AwardSpecialTreasure(special_treasure_light_group->MessageField, time);
}

// 0x425000
float AwardFullCrew(float time)
{
	a_bump2->MessageField = 6;
	if (time >= 0.0f)
	{
		time += 2.0f;
		ShowWithValue(3044, a_bump1->get_scoring(6), time);
	}
	return time;
}

// 0x425c50. One draw: ft::RandIndex(6), from the stream the engine shares (ft_rand.h).
float RandomMysteryAward(float time)
{
	switch (ft::RandIndex(6))
	{
	case 0: return AwardNextSpecialTreasure(time);
	case 1: return AwardExtraBall(time);
	case 2: return AwardFullCrew(time);
	case 3: return AdvanceFathom(time);
	case 4: return AwardMapPiece(time);
	default: return CollectBuriedTreasure(time);
	}
}

// 0x425d10
float CollectMysteryAward(float time)
{
	if (v_lite529->light_on())
	{
		ClearMysteryAward();
		time = RandomMysteryAward(time);
	}
	return time;
}

// 0x429140 (s_rexit8)
void ShipRampExitControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlBallToLayer2)
		return;
	if (info_text_box->MessageField != 4)
	{
		const int amount = ScorePartAmount(caller, 0);
		ShowWithValue(3003, amount, 2.0f);
		PlaySound(tabb40wave, "ShipRampExit");
		return;
	}
	ResetLduggTargets();
	S().ModeCounter++;
	Show(Rc(3018), 2.0f, 0);
	AdvanceFathom(2.0f);
	PlaySound(tabb57wave, "ShipRampExit");
}

// 0x429ef0. The argument is the calling component (the listing shows none / a display time: ScorePart's first
// argument is `[esp+4]`, the callers' `caller`).
void IslandLaneBank(TPinballComponent* caller)
{
	if (island_lane_top_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f) == 3
		&& island_lane_bottom_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f) == 3)
	{
		island_lane_top_light_group->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		island_lane_bottom_light_group->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		AdvanceFathomIsland(0.0f);
		PlaySound(tabb88wave, "IslandLaneBank");
		ScorePart(caller, 1);
		return;
	}
	PlaySound(tabb44wave, "IslandLaneBank");
	ScorePart(caller, 0);
}
}
