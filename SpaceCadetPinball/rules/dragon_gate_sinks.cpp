#include "pch.h"
#include "rules/dragon_rules.h"
#include "TDiverter.h"
#include "TLight.h"
#include "TPinballTable.h"
#include "TSink.h"
#include "TSound.h"
#include "TTextBox.h"
#include "table_text.h"
#include <cstdio>

// Dragon's Keep rules: the two Gate tower sinks (port of DRAGON.EXE 0x4274d0 GateSinkControl1 = a_sink9 and
// 0x427680 GateSinkControl2 = a_sink10; port_listing_dragon.c lines 3472-3693, checked against the machine code).
// The sinks differ only in the tower light and the gate pair they switch back (the "swap" below).
namespace dragon {
namespace {
// 0x4230a0 returns what it added to the score, which the original passes to sprintf. dragon::ScorePart returns nothing,
// so the amount is read back from the running score (same approach as dragon_towers.cpp).
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

// get_rc_string(3010) as the format of crt_sprintf(buf[64], format, value), then Display(buf, 2.0, 0, 0). The format is the
// game's own data; a string the EXE does not provide shows nothing (as DragonRules::TextEvent), the rule state still advances.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
void ShowText3010(int value)
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

// The Gate-1 (a_sink9) swap: tower light off, the lower gate pair back to "tower closed" (0x42750c..0x42754b).
void SwapGate1()
{
	v_lite47->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_dvtraa->Message(MessageCode::TGateEnable, 0.0f);
	a_dvtrab->Message(MessageCode::TGateDisable, 0.0f);
}

// The Gate-2 (a_sink10) swap (0x4276c6..0x427711): v_lite125 off, v_lite126 flashes, gate pair d.
void SwapGate2()
{
	v_lite125->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite126->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	a_dvtrda->Message(MessageCode::TGateEnable, 0.0f);
	a_dvtrdb->Message(MessageCode::TGateDisable, 0.0f);
}

// Both functions react to ControlCollision only. Mode 2 with a tower selected (v_lite50.MessageField != 0): the ball is
// trapped (award). Otherwise (mode != 6) the "no" sound plays and, outside modes 6 and 13, the swap runs; every path but the
// award ends by re-arming a_sink16's timer with its own time (-1).
void GateSink(MessageCode code, TPinballComponent* caller, const char* who, void (*swap)())
{
	if (code != MessageCode::ControlCollision)
		return;
	const int mode = info_text_box->MessageField;
	if (mode == 2 && v_lite50->MessageField != 0)
	{
		tabc10wave->Play(nullptr, who);
		control::TableG->MultiballCount -= 1;
		v_lite50->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		ShowText3010(ScorePartAdded(caller, 0));
		UpdateTowerLocks();
		return;
	}
	if (mode != 6)
	{
		tabc04wave->Play(nullptr, who);
		const int now = info_text_box->MessageField;   // read again after the sound, as the original does
		if (now != 6 && now != 0xd)
			swap();
	}
	a_sink16->Message(MessageCode::TSinkResetTimer, -1.0f);
}
}

void GateSinkControl1(MessageCode code, TPinballComponent* caller) // 0x4274d0, a_sink9
{
	GateSink(code, caller, "GateSinkControl1", SwapGate1);
}

void GateSinkControl2(MessageCode code, TPinballComponent* caller) // 0x427680, a_sink10
{
	GateSink(code, caller, "GateSinkControl2", SwapGate2);
}
}
