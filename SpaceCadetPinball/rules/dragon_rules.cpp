#include "pch.h"
#include "rules/dragon_rules.h"
#include "TBall.h"
#include "TCollisionComponent.h"
#include "TDrain.h"
#include "TFlipper.h"
#include "TKickback.h"
#include "TKickout.h"
#include "TLight.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TSink.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "midi.h"
#include "table_text.h"
#include "timer.h"
#include <cstdio>
#include <cstdlib>

// Dragon's Keep rules: make_links, the table control handler, the cheat keys, UnstuckBall, the text events,
// MissionControl and its mode switch (port of DRAGON.EXE 0x422b80, 0x422fe0, 0x424260, 0x4282c0, 0x42c020, 0x42c120,
// 0x42c320). The mode controllers, the control functions and the other helpers live in the other dragon_*.cpp files.
namespace dragon {
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// Track names of the music list (DRAGON.EXE 0x43bc00..0x43bc28, file identifiers, not text).
const char* const TrackNames[6] = {"tabc7", "tabc1", "tabc2", "tabc4", "tabc5", "tabc6"};
}

DragonRules& R()
{
	DragonRules* rules = dynamic_cast<DragonRules*>(control::Rules);
	if (!rules)
		Fatal("Dragon rule code called without DragonRules");
	return *rules;
}

DragonState& S() { return R().St; }

void MissionControl(int code, TPinballComponent* caller) { R().MissionControl(code, caller); }
void UnselectModeController() { R().UnselectModeController(); }
void ScorePart(TPinballComponent* comp, int index) { R().ScorePart(comp, index); }
void ScoreValue(TPinballComponent* comp, int score) { R().ScoreValue(comp, score); }
void SpecialAddScore(TPinballComponent* comp, int score) { R().SpecialAddScore(comp, score); }
void AddJackpot(int n) { R().AddJackpot(n); }
bool CheckBallInControlBounds(TBall* ball, TPinballComponent* part) { return R().CheckBallInControlBounds(ball, part); }
void SetMode(int mode) { R().SetMode(mode); }
int Mode() { return R().Mode(); }
void PlayTrack(int handle) { midi::play_track(handle, true); }

void MissionControlSwitch(int mode, int code, TPinballComponent* caller)
{
	switch (mode)
	{
	case 0: Mode0GameOverController(code); return;
	case 1: return;
	case 2: Mode2WaitQuestController(code); return;
	case 3: Mode3Controller(code, caller); return;
	case 4: Mode4Controller(code, caller); return;
	case 5: Mode5Controller(code, caller); return;
	case 6: Mode6Controller(code, caller); return;
	case 7: Mode7Controller(code, caller); return;
	case 8: Mode8Controller(code, caller); return;
	case 9: Mode9Controller(code, caller); return;
	case 10: Mode10LaunchBallController(code, caller); return;
	case 11: Mode11BonusController(code, caller); return;
	case 12: Mode12EggShopController(code, caller); return;
	case 13: Mode13Controller(code); return;
	case 14: Mode14Controller(code); return;
	case 15: Mode15Controller(code); return;
	default: UnselectModeController(); return;
	}
}
}

void DragonRules::SetMode(int m)
{
	if (!dragon::info_text_box)
		dragon::Fatal("SetMode before the info text box is linked");
	dragon::info_text_box->MessageField = m;
}

int DragonRules::Mode() const
{
	if (!dragon::info_text_box)
		dragon::Fatal("Mode before the info text box is linked");
	return dragon::info_text_box->MessageField;
}

void DragonRules::MissionControl(int code, TPinballComponent* caller)
{
	using namespace dragon;
	if (!info_text_box)
		return;
	if (caller && caller == drain && code == static_cast<int>(MessageCode::ControlCollision) && control::TableG->MultiballCount == 0)
		St.BallDrained = 1;
	if (caller)
	{
		if (v_lite28->light_on() && caller->TypeId == 1019) // TSoloTarget
			dragon::AddSuperJackpot(100000);
		int jackpot = 0;
		if (caller->TypeId == 1006 || caller->TypeId == 1019) // TPopupTarget, TSoloTarget
		{
			jackpot = 50000;
			St.TargetsHit++;
		}
		else if (caller->TypeId == 1017 && (caller == a_sink4 || caller == a_sink1)) // TSink
			jackpot = 75000;
		if (jackpot)
			AddJackpot(jackpot);
	}
	if (code == static_cast<int>(MessageCode::Resume) ||
		(code == static_cast<int>(MessageCode::ControlTimerExpired) && info_text_box == caller && !info_text_box->HasPendingMessage()))
		code = static_cast<int>(MessageCode::ControlMissionStarted);
	dragon::MissionControlSwitch(info_text_box->MessageField, code, caller);
}

void DragonRules::MakeLinks(TPinballTable* table)
{
	using namespace dragon;
	control::TableG = table;
	C.LinkFailed = 0;
	for (component_info& entry : score_components)
	{
		TPinballComponent* comp = MakeComponentLink(entry.Tag);
		if (comp)
			comp->Control = &entry.Control;
	}
	for (component_tag_base* tag : simple_components)
		MakeComponentLink(*tag);
	if (C.LinkFailed)
		Fatal("make_links: a component link failed (see the messages above)");
	InfoBox = info_text_box;
	BallSaveLight = v_lite40;
	ReboundLight[0] = v_lite120;
	ReboundLight[1] = v_lite121;
}

void DragonRules::ClearLinks()
{
	using namespace dragon;
	if (C.TickerTimer)
		timer::kill(C.TickerTimer);
	C.TickerTimer = 0;
	C.LastScorer = nullptr;
	InfoBox = nullptr;
	BallSaveLight = nullptr;
	ReboundLight[0] = ReboundLight[1] = nullptr;
	for (component_info& entry : score_components)
		entry.Tag.SetComponent(nullptr);
	for (component_tag_base* tag : simple_components)
		tag->SetComponent(nullptr);
	control::TableG = nullptr;
}

void DragonRules::TableControlHandler(MessageCode code)
{
	using namespace dragon;
	switch (code)
	{
	case MessageCode::SetTiltLock:
		St.Vestige = 0;
		dragon::ResetSpells();
		a_kick1->ActiveFlag = 0;
		return;
	case MessageCode::NewGame:
		St.LastBall = 0;
		v_lite113->ActiveFlag = 0;
		v_lite114->ActiveFlag = 0;
		v_lite115->ActiveFlag = 0;
		dragon::SetSwordState(3);
		if (midi::active_track() != St.TrackNewGame)
			midi::play_track(St.TrackNewGame, true);
		C.BallSave = 3;
		v_lite40->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		control::TableG->JackpotScore = 1000000;
		control::TableG->SuperJackpotScore = 2500000;
		info_text_box->MessageField = 1;
		return;
	case MessageCode::TableBuilt:
	{
		St.TrackNewGame = midi::load_track(TrackNames[0]);
		St.TrackA = midi::load_track(TrackNames[1]);
		St.TrackMode4 = midi::load_track(TrackNames[2]);
		St.TrackB = St.TrackA; // copied before the default substitution
		St.TrackMode6 = midi::load_track(TrackNames[3]);
		St.TrackMode7 = midi::load_track(TrackNames[4]);
		St.TrackMode8 = midi::load_track(TrackNames[5]);
		const int firstLoaded = St.TrackA;
		St.TrackGameOver = St.TrackA;
		int* const missing[] = {&St.TrackA, &St.TrackMode4, &St.TrackB, &St.TrackMode6, &St.TrackMode7, &St.TrackMode8};
		for (int* slot : missing)
			if (*slot == 0)
				*slot = St.TrackNewGame;
		if (firstLoaded == 0)
			St.TrackGameOver = St.TrackNewGame;
		v_lite113->ActiveFlag = 0;
		v_lite114->ActiveFlag = 0;
		v_lite115->ActiveFlag = 0;
		return;
	}
	case MessageCode::GameOver:
		C.BallSave = 0;
		v_lite40->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite113->ActiveFlag = 0;
		v_lite114->ActiveFlag = 0;
		v_lite115->ActiveFlag = 0;
		return;
	default:
		return;
	}
}

void DragonRules::BackdoorKey(int key, bool isLeftFlip, bool isRightFlip)
{
	using namespace dragon;
	if (Mode() == 11 && St.LastBall != 0 && (isLeftFlip || isRightFlip))
		MissionControl(static_cast<int>(MessageCode::TFlipperExtend), nullptr);
	if (C.QuoteFlag != 0)
		TextEvent(0x69);

	// The cheat words: 1MAX (extra ball), NMAX (start the selected quest), PB (alternate ball), QUOTE (quotes ticker);
	// FMAX / PMAX / QMAX are accepted letter by letter and then do nothing.
	bool skipReset = false;
	auto next = [&](int expectedPrev, int family)
	{
		const bool ok = family ? St.CheatState == 0 : St.CheatState == expectedPrev;
		if (ok)
		{
			if (family)
				St.CheatMode = family;
			St.CheatState = key;
			skipReset = true;
		}
	};
	switch (key)
	{
	case 0x31: next(0, 1); break;
	case 0x46: next(0, 3); break;
	case 0x4e: next(0, 2); break;
	case 0x50: next(0, 4); break;
	case 0x51: next(0, 5); break;
	case 0x41: next(0x4d, 0); break;
	case 0x4f: next(0x55, 0); break;
	case 0x54: next(0x4f, 0); break;
	case 0x55: next(0x51, 0); break;
	case 0x4d:
		if (St.CheatState == 0x31 || St.CheatState == 0x46 || St.CheatState == 0x4e)
		{
			St.CheatState = key;
			skipReset = true;
		}
		break;
	case 0x42:
		if (St.CheatState != 0x50)
			skipReset = true;
		else
			TBall::AltBallIndex = 1 - TBall::AltBallIndex;
		break;
	case 0x45:
		if (St.CheatState != 0x54)
			skipReset = true;
		else
			TextEvent(0x6a);
		break;
	case 0x58:
		if (St.CheatState != 0x41)
			skipReset = true;
		else if (St.CheatMode == 1)
		{
			dragon::table_add_extra_ball(2.0f);
			control::TableG->CheatsUsed = 1;
		}
		else if (St.CheatMode != 2)
			skipReset = true;
		else if (Mode() == 2)
		{
			St.BallHeld = 1;
			if (dragon::StartSelectedQuest() == 0)
				St.BallHeld = 0;
			else
				control::TableG->CheatsUsed = 1;
		}
		break;
	default:
		break;
	}
	if (!skipReset)
		St.CheatState = 0;
	if (St.CheatState == 0)
		St.CheatMode = 0;
}

void DragonRules::UnstuckBall(TBall& ball, int)
{
	using namespace dragon;
	if (CheckBallInControlBounds(&ball, a_flip1) || CheckBallInControlBounds(&ball, a_flip3) ||
		CheckBallInControlBounds(&ball, a_flip2) || CheckBallInControlBounds(&ball, s_plunger1))
		return;
	if (ball.StuckCounter > 20)
	{
		ball.Disable();
		control::TableG->MultiballCount--;
		s_plunger1->Message(MessageCode::PlungerRelaunchBall, 0.0f);
		return;
	}
	vector3 throwDir{0.0f, -1.0f, 0.0f}; // DRAGON.EXE 0x43bb98
	ball.throw_ball(&throwDir, 90.0f, 1.0f, 0.0f);
}

void DragonRules::TextEvent(int ftCode)
{
	using namespace dragon;
	struct Entry { int code; int id; float time; };
	static const Entry entries[] = {
		{0x59, 2003, -1.0f}, {0x5a, 2002, -1.0f}, {0x5b, 2004, 0.5f}, {0x5c, 2001, -1.0f}, {0x5d, 2005, 2.0f},
		{0x5e, 2006, -1.0f}, {0x60, 2015, -1.0f}, {0x61, 2007, -1.0f}, {0x62, 2016, -1.0f}, {0x63, 2008, -1.0f},
		{0x64, 2017, -1.0f}, {0x65, 2009, -1.0f}, {0x66, 2018, -1.0f}, {0x67, 2010, -1.0f}, {0x6b, 2038, -1.0f},
		{0x6c, 2039, -1.0f}, {0x6d, 2036, -1.0f}, {0x6e, 2037, -1.0f},
	};
	if (!InfoBox)
		Fatal("text event before the info text box is linked");
	switch (ftCode)
	{
	case 0x57:
		InfoBox->Clear();
		return;
	case 0x58:
		// FT 0x41dc50: the GDI (system font) draw of the overlay / head text, no background copy, no state change.
		// Natively TTextBox::DrawImGui draws that text every frame from the same state, so there is nothing to do here.
		return;
	case 0x5f:
		if (St.LastBall == 0)
			InfoBox->Clear();
		return;
	case 0x68:
		C.QuoteFlag = 0;
		TickerStart(true);
		return;
	case 0x69:
		TickerStop();
		return;
	case 0x6a:
		C.QuoteFlag = 1;
		TickerStart(true);
		return;
	default:
		break;
	}
	for (const Entry& e : entries)
	{
		if (e.code != ftCode)
			continue;
		const char* text = table_text::Get(e.id);
		if (!text)
		{
			std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), event 0x%x shows nothing\n", e.id, ftCode);
			return;
		}
		InfoBox->Display(text, e.time, 0, false);
		return;
	}
}

const char* DragonRules::GetText(int id)
{
	return table_text::Get(id);
}

const char* const* DragonRules::CheatNames(int& count)
{
	static const char* const names[] = {"1max", "nmax", "pb", "quote"};
	count = 4;
	return names;
}

void DragonRules::DumpState(std::vector<std::pair<const char*, int>>& out)
{
	using namespace dragon;
	// The Dragon keys of re/test/scripts/watch_dragon.json; component-held state reads 0 before the links exist.
	out.emplace_back("mode", info_text_box ? info_text_box->MessageField : 0);
	out.emplace_back("saved_quest", mode_timer ? mode_timer->MessageField : 0);
	out.emplace_back("selected_quest", a_kout2 ? a_kout2->MessageField : 0);
	out.emplace_back("quests_done", v_lite28 ? v_lite28->MessageField : 0);
	out.emplace_back("held_bonus", drain ? drain->MessageField : 0);
	out.emplace_back("ball_save", C.BallSave);
	out.emplace_back("drain_state", St.DrainState);
	out.emplace_back("last_ball", St.LastBall);
	out.emplace_back("ball_drained", St.BallDrained);
	out.emplace_back("ball_held", St.BallHeld);
	out.emplace_back("targets_hit", St.TargetsHit);
	out.emplace_back("spells", St.Spells);
	out.emplace_back("bonus_step", St.BonusStep);
}
