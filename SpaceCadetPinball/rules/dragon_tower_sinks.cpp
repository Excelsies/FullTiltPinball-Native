#include "pch.h"
#include "rules/dragon_rules.h"
#include "TLight.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TSink.h"
#include "TSound.h"
#include "TTextBox.h"
#include "table_text.h"
#include <cstdio>

// Dragon's Keep rules: the Prison and Watch tower sinks (port of DRAGON.EXE 0x426f90 PrisonSinkControl1
// (a_sink15), 0x427100 PrisonSinkControl2 (a_sink18), 0x427290 WatchSinkControl1 (a_sink13), 0x4273d0 WatchSinkControl2
// (a_sink17); port_listing_dragon.c lines 2999-3471). No rule globals; the tower-lock state lives in the tower lights'
// MessageField (UpdateTowerLocks / ResetTowerLocks in dragon_towers.cpp).
namespace dragon {
namespace {
// What 0x4230a0 (ScorePart) returns, which the original passes to sprintf: the amount added to the running score.
// dragon::ScorePart returns nothing, so the amount is read back from the score (same helper as dragon_towers.cpp).
long long RunningScore()
{
	TPinballTable* table = control::TableG;
	return static_cast<long long>(table->CurScoreE9) * 1000000000LL + table->CurScore;
}

int ScorePartAdded(TPinballComponent* comp, int index)
{
	const long long before = RunningScore();
	ScorePart(comp, index);
	return static_cast<int>(RunningScore() - before);
}

// get_rc_string(3010) as the format of crt_sprintf(buf[64], format, value), then Display(buf, 2.0, 0, 0).
// The format is the game's own data; a string the EXE does not provide shows nothing (the reader has logged why).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
void ShowTrappedText(int value)
{
	const char* format = table_text::Get(3010);
	if (!format)
	{
		std::fprintf(stderr, "FT rules: text 3010 unavailable (EXE missing or string absent), shows nothing\n");
		return;
	}
	char buffer[64];
	std::snprintf(buffer, sizeof buffer, format, static_cast<long>(value));
	info_text_box->Display(buffer, 2.0f, 0, false);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// A ball locked in the lit tower: shared tail of the four controls (0x426fbc, 0x427155, 0x4272ed, 0x427417).
// The shared prefix of the original is: sound, one ball less in play, the tower light solid, the plunger feeds the next
// ball after 0.96 s, the part's score (one ScorePart call whose result is the text's value), the text, tower state update.
void TrapBall(TLight* towerLight, TPinballComponent* caller)
{
	tabc10wave->Play(nullptr, "TowerSink");
	control::TableG->MultiballCount--;
	towerLight->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	s_plunger1->Message(MessageCode::PlungerRelaunchBall, 0.96f);
	ShowTrappedText(ScorePartAdded(caller, 0));
	UpdateTowerLocks();
}

// Mode 6, Prison sinks (0x427075 / 0x427203): sound, one draw RandIndex(2) chooses which eject sink resets and which light flashes.
void PrisonMode6()
{
	tabc51wave->Play(nullptr, "TowerSink");
	if (ft::RandIndex(2) != 0)
	{
		a_sink1->Message(MessageCode::TSinkResetTimer, -1.0f);
		v_lite66->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		return;
	}
	a_sink2->Message(MessageCode::TSinkResetTimer, -1.0f);
	v_lite65->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
}
}

void PrisonSinkControl1(MessageCode code, TPinballComponent* caller) // 0x426f90, a_sink15
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField == 6)
	{
		PrisonMode6();
		return;
	}
	if (v_lite48->MessageField != 0)
	{
		TrapBall(v_lite48, caller);
		return;
	}
	a_sink18->Message(MessageCode::TSinkResetTimer, -1.0f);
	tabc04wave->Play(nullptr, "TowerSink");
}

void PrisonSinkControl2(MessageCode code, TPinballComponent* caller) // 0x427100, a_sink18
{
	if (code != MessageCode::ControlCollision)
		return;
	const int mode = info_text_box->MessageField;
	if (mode == 2)
	{
		if (v_lite48->MessageField != 0)
		{
			TrapBall(v_lite48, caller);
			return;
		}
		a_sink18->Message(MessageCode::TSinkResetTimer, 0.0f);
		UpdateTowerLocks();
		return;
	}
	if (mode != 6)
	{
		a_sink18->Message(MessageCode::TSinkResetTimer, -1.0f);
		ResetTowerLocks();
		return;
	}
	PrisonMode6();
}

void WatchSinkControl1(MessageCode code, TPinballComponent* caller) // 0x427290, a_sink13
{
	if (code != MessageCode::ControlCollision)
		return;
	const int mode = info_text_box->MessageField;
	if (mode == 2)
	{
		if (v_lite49->MessageField != 0)
		{
			TrapBall(v_lite49, caller);
			return;
		}
		tabc04wave->Play(nullptr, "TowerSink");
		a_sink17->Message(MessageCode::TSinkResetTimer, -1.0f);
		return;
	}
	if (mode != 6)
		tabc04wave->Play(nullptr, "TowerSink");
	a_sink17->Message(MessageCode::TSinkResetTimer, -1.0f);
}

void WatchSinkControl2(MessageCode code, TPinballComponent* caller) // 0x4273d0, a_sink17
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField != 2)
	{
		a_sink17->Message(MessageCode::TSinkResetTimer, 0.0f);
		ResetTowerLocks();
		return;
	}
	if (v_lite49->MessageField != 0)
	{
		TrapBall(v_lite49, caller);
		return;
	}
	a_sink17->Message(MessageCode::TSinkResetTimer, 0.0f);
	UpdateTowerLocks();
}
}
