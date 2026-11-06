#include "pch.h"
#include "rules/pirates_rules.h"
#include "midi.h"
#include "pb.h"
#include "table_text.h"
#include <cstdio>
#include "TBall.h"
#include "TFlipper.h"
#include "TKickback.h"
#include "TKickout.h"
#include "TLight.h"
#include "TOneway.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "TWall.h"
#include "TDrain.h"
#include <cstdio>
#include <cstdlib>

using namespace pirates;

// Skulduggery rules: the TableRules entry points, the mission dispatcher and the cheat keys.
// Ported from port_listing_pirates.c (FT 0x422b90, 0x423000, 0x4230c0, 0x42a830, 0x42d850, 0x42d900, 0x42db30).
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

PiratesRules& Rules()
{
	PiratesRules* r = dynamic_cast<PiratesRules*>(control::Rules);
	if (!r)
		Fatal("Skulduggery rules function called without PiratesRules");
	return *r;
}

// FT text codes 0x57..0x6e that display a resource string: (code, string id, display time). -1.0 = until replaced.
struct TextCase { int code; int id; float time; };
const TextCase TextCases[] = {
	{0x59, 2003, -1.0f}, {0x5a, 2002, -1.0f}, {0x5b, 2004, 0.5f}, {0x5c, 2001, -1.0f}, {0x5d, 2005, 2.0f},
	{0x5e, 2006, -1.0f}, {0x60, 2015, 1.0f}, {0x61, 2007, 1.0f}, {0x62, 2016, 1.0f}, {0x63, 2008, 1.0f},
	{0x64, 2017, 1.0f}, {0x65, 2009, 1.0f}, {0x66, 2018, 1.0f}, {0x67, 2010, 1.0f}, {0x6b, 2038, -1.0f},
	{0x6c, 2039, -1.0f}, {0x6d, 2036, -1.0f}, {0x6e, 2037, -1.0f}};

const char* const CheatList[] = {"1MAX", "FMAX", "GMAX", "NMAX", "QUOTE"};
}

namespace pirates {
State& S() { return Rules().St; }
int& BallSave() { return Rules().BallSaveRef(); }
TPinballComponent*& LastScorer() { return Rules().LastScorerRef(); }
int& QuoteFlag() { return Rules().QuoteFlagRef(); }
void MissionControl(int code, TPinballComponent* caller) { Rules().Mission(code, caller); }
void SetMode(int mode) { Rules().SetModePub(mode); }
int Mode() { return Rules().ModePub(); }
int ScorePart(TPinballComponent* comp, int index) { return Rules().ScorePartPub(comp, index); }
int ScoreValue(TPinballComponent* comp, int score) { return Rules().ScoreValuePub(comp, score); }
void SpecialAddScore(TPinballComponent* comp, int score) { Rules().SpecialAddScorePub(comp, score); }
void AddJackpot(int n) { Rules().AddJackpotPub(n); }
void UnselectModeController() { Rules().UnselectPub(); }
bool CheckBallInControlBounds(TBall* ball, TPinballComponent* part) { return Rules().InBoundsPub(ball, part); }
void TickerStart(bool fromStart) { Rules().TickerStartPub(fromStart); }
void TickerStop() { Rules().TickerStopPub(); }
}

void PiratesRules::SetMode(int m)
{
	if (!info_text_box)
		Fatal("SetMode before the info text box is linked");
	info_text_box->MessageField = m;
}

int PiratesRules::Mode() const
{
	if (!info_text_box)
		Fatal("Mode before the info text box is linked");
	return info_text_box->MessageField;
}

// 0x423000 make_links
void PiratesRules::MakeLinks(TPinballTable* table)
{
	control::TableG = table;
	for (auto& entry : pirates::score_components)
	{
		TPinballComponent* comp = MakeComponentLink(entry.Tag);
		if (comp)
			comp->Control = &entry.Control;
	}
	for (component_tag_base* tag : pirates::simple_components)
		MakeComponentLink(*tag);
	if (C.LinkFailed)
		Fatal("make_links: a component link failed (assert_box in the original)");
	// the members the shared ft_common.cpp functions read
	InfoBox = pirates::info_text_box;
	BallSaveLight = pirates::v_lite569;
	ReboundLight[0] = pirates::v_lite84;
	ReboundLight[1] = pirates::v_lite85;
	Linked = true;
}

void PiratesRules::ClearLinks()
{
	if (Linked)
		TickerStop(); // no ticker timer may outlive the rules
	Linked = false;
	control::TableG = nullptr;
	for (auto& entry : pirates::score_components)
		entry.Tag.SetComponent(nullptr);
	for (component_tag_base* tag : pirates::simple_components)
		tag->SetComponent(nullptr);
	InfoBox = nullptr;
	BallSaveLight = nullptr;
	ReboundLight[0] = ReboundLight[1] = nullptr;
}

// 0x4230c0 table_control_handler
void PiratesRules::TableControlHandler(MessageCode code)
{
	using namespace pirates;
	if (code == MessageCode::SetTiltLock)
	{
		SetMode(17);
		MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
		return;
	}
	if (code == MessageCode::TableBuilt)
	{
		// six tracks; a track that does not load takes the first one (slots 1..5 and 6), as the original
		St.Music[0] = midi::load_track("tabb1");
		St.Music[1] = midi::load_track("tabb2");
		St.Music[2] = midi::load_track("tabb3");
		St.Music[3] = midi::load_track("tabb4");
		St.Music[4] = midi::load_track("tabb5");
		St.Music[5] = midi::load_track("tabb6");
		St.Music[6] = St.Music[4]; // 0x43ab98 takes tabb5's own handle before the fallback below
		for (int i = 1; i <= 5; ++i)
			if (St.Music[i] == 0)
				St.Music[i] = St.Music[0];
		if (St.Music[6] == 0)
			St.Music[6] = St.Music[0];
		return;
	}
	if (code != MessageCode::NewGame)
		return;
	control::TableG->JackpotScore = 1000000;
	C.BallSave = 0;
	s_oneway12->ActiveFlag = 0;
	St.LastBall = 0;
	St.Mode11Done = 0;
	St.DigState[0] = 0;      // 0x435d10
	St.DigState[2] = -1;     // 0x435d18
	St.DigState[3] = -1;     // 0x435d1c
	St.DigState[4] = 0;      // 0x435d20
	St.DigState[5] = 0;      // 0x435d24
	St.DigState[6] = 0;      // 0x435d28
	St.DigState[1] = 1;      // 0x435d14
	TreasureXLightsOff();
	OrbitSinksInactive();
	if (midi::active_track() != St.Music[0])
		midi::play_track(St.Music[0], false);
	St.LockedBalls = 0;
	C.LastScorer = nullptr;
	a_kick2->ActiveFlag = 0;
	s_bwal610->ActiveFlag = 0;
	a_kick1->Message(MessageCode::FtPartDisable, 0.0f);
	a_kout1->Message(MessageCode::FtPartDisable, 0.0f);
	a_kout2->Message(MessageCode::FtPartDisable, 0.0f);
	SetModeLaunch();
	SetMode(0);
}

// 0x42d850 MissionControl
void PiratesRules::MissionControl(int code, TPinballComponent* caller)
{
	using namespace pirates;
	for (;;)
	{
		if (!info_text_box)
			return;
		if (caller)
		{
			if (caller->TypeId == 1006) // TPopupTarget
			{
				++St.TargetsHit;
				AddJackpot(20000);
			}
			else if (caller->TypeId == 1017) // TSink
				AddJackpot(40000);
			else if (caller->TypeId == 1019) // TSoloTarget
				AddJackpot(20000);
		}
		if (code == static_cast<int>(MessageCode::ControlTimerExpired) && caller == info_text_box && !info_text_box->CurrentMessage)
			code = static_cast<int>(MessageCode::ControlMissionStarted);
		MissionControlSwitch(info_text_box->MessageField, code, caller);
		if (code != static_cast<int>(MessageCode::ControlModeEnd))
			return;
		caller = nullptr;
		code = static_cast<int>(MessageCode::ControlMissionComplete);
		info_text_box->MessageField = 2;
	}
}

// 0x42d900 MissionControlSwitch
void PiratesRules::MissionControlSwitch(int mode, int code, TPinballComponent* caller)
{
	using namespace pirates;
	if (code == static_cast<int>(MessageCode::ControlMissionComplete))
		SetModeLight(mode, 1);
	else if (code == static_cast<int>(MessageCode::ControlModeEnd))
	{
		if (mode > 3 && mode < 12)
			AddJackpot(750000);
		SetModeLight(mode, 0);
		SelectNextMode();
	}
	switch (mode)
	{
	case 0: return;
	case 1: Mode1GameOverController(code, caller); return;
	case 2: Mode2IdleController(code, caller); return;
	case 3: Mode3LaunchController(code, caller); return;
	case 4: Mode4Controller(code, caller); return;
	case 5: Mode5Controller(code, caller); return;
	case 6: Mode6Controller(code, caller); return;
	case 7: Mode7Controller(code, caller); return;
	case 8: Mode8Controller(code, caller); return;
	case 9: Mode9Controller(code, caller); return;
	case 10: Mode10Controller(code, caller); return;
	case 11: Mode11Controller(code, caller); return;
	case 12: Mode12Controller(code, caller); return;
	case 13: Mode13Controller(code, caller); return;
	case 16: Mode16BonusController(code, caller); return;
	case 17: Mode17TiltController(code, caller); return;
	default: UnselectModeController(); return; // modes 14, 15 and anything else
	}
}

// 0x42db30 UnstuckBall
void PiratesRules::UnstuckBall(TBall& ball, int)
{
	using namespace pirates;
	if (CheckBallInControlBounds(&ball, a_flip1) || CheckBallInControlBounds(&ball, a_flip3)
		|| CheckBallInControlBounds(&ball, a_flip2) || CheckBallInControlBounds(&ball, a_flip4)
		|| CheckBallInControlBounds(&ball, s_launcher1) || CheckBallInControlBounds(&ball, s_oneway12)
		|| CheckBallInControlBounds(&ball, a_kick1))
		return;
	if (ball.StuckCounter > 20)
	{
		ball.Disable();
		control::TableG->MultiballCount--;
		s_launcher1->Message(MessageCode::PlungerRelaunchBall, 0.0f);
		return;
	}
	vector3 throwDir{0.0f, -1.0f, 0.0f}; // 0x43d160
	ball.throw_ball(&throwDir, 90.0f, 1.0f, 0.0f);
}

// 0x42a830 pbctrl_bdoor_controller
void PiratesRules::BackdoorKey(int key, bool isLeftFlip, bool isRightFlip)
{
	using namespace pirates;
	if (info_text_box->MessageField == 0x10 && St.LastBall != 0 && (isLeftFlip || isRightFlip))
		MissionControl(static_cast<int>(MessageCode::TFlipperExtend), nullptr);
	if (C.QuoteFlag != 0)
		TextEvent(0x69);

	bool keep = false; // true = the original jumps to LAB_0042aa58 and keeps the cheat state
	switch (key)
	{
	case 0x31: if (St.CheatKey == 0) { St.CheatKind = 1; St.CheatKey = key; keep = true; } break;
	case 0x41: if (St.CheatKey == 0x4d) { St.CheatKey = key; keep = true; } break;
	case 0x45:
		if (St.CheatKey != 0x54) { keep = true; break; }
		TextEvent(0x6a);
		break;
	case 0x46: if (St.CheatKey == 0) { St.CheatKind = 3; St.CheatKey = key; keep = true; } break;
	case 0x47: if (St.CheatKey == 0) { St.CheatKind = 4; St.CheatKey = key; keep = true; } break;
	case 0x4d:
		if (St.CheatKey == 0x31 || St.CheatKey == 0x46 || St.CheatKey == 0x4e || St.CheatKey == 0x47)
		{
			St.CheatKey = key;
			keep = true;
		}
		break;
	case 0x4e: if (St.CheatKey == 0) { St.CheatKind = 2; St.CheatKey = key; keep = true; } break;
	case 0x4f: if (St.CheatKey == 0x55) { St.CheatKey = key; keep = true; } break;
	case 0x51: if (St.CheatKey == 0) { St.CheatKind = 5; St.CheatKey = key; keep = true; } break;
	case 0x54: if (St.CheatKey == 0x4f) { St.CheatKey = key; keep = true; } break;
	case 0x55: if (St.CheatKey == 0x51) { St.CheatKey = key; keep = true; } break;
	case 0x58:
		if (St.CheatKey != 0x41) { keep = true; break; }
		if (St.CheatKind == 1)
		{
			table_add_extra_ball(2.0f);
			control::TableG->CheatsUsed = 1;
		}
		else if (St.CheatKind == 2)
		{
			if (info_text_box->MessageField == 2)
			{
				St.NoHeldBall = 1;
				StartSelectedMode();
				control::TableG->CheatsUsed = 1;
			}
		}
		else
		{
			if (St.CheatKind != 4) { keep = true; break; }
			St.Gmax = 1 - St.Gmax;
		}
		break;
	default: break;
	}
	if (!keep)
		St.CheatKey = 0;
	if (St.CheatKey == 0)
		St.CheatKind = 0;
}

// 0x422b90 FtTextEvent
void PiratesRules::TextEvent(int ftCode)
{
	if (!info_text_box)
		Fatal("text event before the info text box is linked");
	TTextBox* box = info_text_box;
	for (const TextCase& tc : TextCases)
	{
		if (tc.code == ftCode)
		{
			const char* text = table_text::Get(tc.id);
			if (!text)
				Fatal("TextEvent: string resource unavailable");
			box->Display(text, tc.time, 0, false);
			return;
		}
	}
	switch (ftCode)
	{
	case 0x57: box->Clear(); return;
	case 0x58:
		// FT 0x41dc50 is the GDI branch of the text box draw: when the box has no bitmap font (+0x5a == 0) it calls
		// DrawTextA (0x4097e0) with the overlay or head text, without a background copy or any state change. Natively
		// that text is drawn every frame by TTextBox::DrawImGui from the same state, so there is nothing to do here.
		return;
	case 0x5f:
		if (St.LastBall == 0)
			box->Clear();
		return;
	case 0x68: C.QuoteFlag = 0; TickerStart(true); return;
	case 0x69: C.QuoteFlag = 0; TickerStop(); return;
	case 0x6a: C.QuoteFlag = 1; TickerStart(true); return;
	default: return;
	}
}

const char* PiratesRules::GetText(int id) { return table_text::Get(id); }

const char* const* PiratesRules::CheatNames(int& count)
{
	count = static_cast<int>(sizeof CheatList / sizeof CheatList[0]);
	return CheatList;
}

// The Skulduggery rule keys of re/test/scripts/watch_pirates.json (not the TableG ones: the hook writes those).
void PiratesRules::DumpState(std::vector<std::pair<const char*, int>>& out)
{
	if (!Linked)
		return; // before MakeLinks / after ClearLinks there is nothing to read
	using namespace pirates;
	out.emplace_back("mode", info_text_box->MessageField);
	out.emplace_back("selected_mode", mode_timer->MessageField);
	out.emplace_back("modes_played", v_lite606->MessageField);
	out.emplace_back("map_pieces", v_lite607->MessageField);
	out.emplace_back("skill_index", s_launcher1->MessageField);
	out.emplace_back("letters", v_lite1->MessageField);
	out.emplace_back("held_bonus", drain->MessageField);
	out.emplace_back("ball_save", C.BallSave);
	out.emplace_back("drain_state", St.DrainState);
	out.emplace_back("last_ball", St.LastBall);
	out.emplace_back("locked", St.LockedBalls);
	out.emplace_back("targets_hit", St.TargetsHit);
	out.emplace_back("no_hold_start", St.NoHeldBall);
	out.emplace_back("bonus_step", St.BonusStep);
	out.emplace_back("mode11_done", St.Mode11Done);
}
