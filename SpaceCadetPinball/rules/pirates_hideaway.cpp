#include "pch.h"
#include "rules/pirates_rules.h"
#include "table_text.h"
#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TSink.h"
#include "TSound.h"
#include "TTextBox.h"
#include <cstdio>
#include <cstdlib>

using namespace pirates;

// Skulduggery rules: the three Hideaway sinks, their award and light helpers and the
// three mode 7 sinks (a_sink5-7). Ported from port_listing_pirates.c lines 669-696 (HideawayExit), 697-744 (AwardCoins), 765-841 (hideaway
// lights), 3444-3672 (HideawaySinkControl1-3) and 3919-4034 (Mode7SinkBank, Mode7SinkControl1-3); FT 0x4236a0..0x4239a0,
// 0x427b90..0x428120, 0x428900..0x428b00 (PIRATES.EXE). Checked against the disassembly where the listing lost arguments
// (the chained display time, Message values): every Message value below was read from the push sequence of the call.
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// Coin ladder, FT 0x43d0c0 (7 dwords): indexed by the coin light group's lit count after the step.
const int CoinLadder[7] = {0, 50000, 100000, 200000, 400000, 800000, 1600000};
// Glyph strings are font-1 character codes, not text: coin icon (FT 0x43aaf8) and the mode 7 sink pattern for the lit
// bits 0..7 (FT 0x43aaa0 + 4 * bits).
const char CoinGlyph[] = "D";
const char* const Mode7SinkGlyph[8] = {"333", "334", "343", "344", "433", "434", "443", "444"};

// TTextBox::Display(text, time, font, low priority) as the original's four-argument call (FT 0x41de40).
void Show(const char* text, float time, int font)
{
	info_text_box->Display(text, time, font, false);
}

// crt_sprintf(buf, get_rc_string(id), value) + Display(buf, time, 0, 0). The long cast satisfies %ld and %d alike on x86-64.
void ShowScoreText(int stringId, int value, float time)
{
	const char* format = table_text::Get(stringId);
	if (!format)
		Fatal("hideaway: table text id missing from the EXE");
	char buf[64];
	std::snprintf(buf, sizeof buf, format, static_cast<long>(value));
	Show(buf, time, 0);
}

// One of the three hideaway sinks (HideawaySinkControl1-3): the sinks differ in their lights, their bit, the
// reward of the mode 8 case and in whether case 2 flashes the exit light first (sinks 2 and 3 do, sink 1 does not).
struct HideawaySink
{
	TLight*& ExitLight;      // v_lite519 / 520 / 521: the flasher the control ends with
	TLight*& BitLight;       // v_lite524 / 525 / 526: this sink's light in the hideaway set
	TLight*& TreasureLight;  // v_mlite44 / 107 / 106: mode 8 light
	int Bit;                 // 1 / 2 / 4 in v_lite524->MessageField
	bool PreFlash;           // case 2 starts with ExitLight->Message(TLightFlasherStartTimedThenStayOn, 1.0)
	bool Jackpot;            // case 8 hit on a lit TreasureLight: AwardJackpot (sink 1) / AwardNextSpecialTreasure (2, 3)
};

void HideawaySinkBody(const HideawaySink& s, MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	switch (info_text_box->MessageField)
	{
	case 2:
		if (s.PreFlash)
			s.ExitLight->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		ShowScoreText(3002, ScorePart(caller, 0), 2.0f);
		s.BitLight->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite524->MessageField = v_lite524->MessageField | s.Bit;
		CheckHideawayComplete(2.0f);
		tabb11wave->Play(nullptr, "HideawaySinkControl");
		HideawayExit();
		break;
	default:
		ShowScoreText(3002, ScorePart(caller, 0), 2.0f);
		tabb11wave->Play(nullptr, "HideawaySinkControl");
		HideawayExit();
		break;
	case 5:
		ScorePart(caller, 0);
		tabb70wave->Play(nullptr, "HideawaySinkControl");
		LightMysteryAward(0.0f);
		HideawayExit();
		break;
	case 6:
		HideawayExit();
		tabb11wave->Play(nullptr, "HideawaySinkControl");
		break;
	case 8:
		if (!s.TreasureLight->FlasherOnFlag)
		{
			ShowScoreText(3002, ScorePart(caller, 0), 2.0f);
			tabb11wave->Play(nullptr, "HideawaySinkControl");
			HideawayExit();
		}
		else
		{
			s.TreasureLight->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
			s.BitLight->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			s.ExitLight->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			if (s.Jackpot)
				AwardJackpot(0.0f);
			else
				AwardNextSpecialTreasure(0.0f);
			LightNextTreasureX();
			ReleaseHoldSink(-1.0f);
		}
		break;
	case 0xb:
		AwardNextSpecialTreasure(0.0f);
		HideawayExit();
		break;
	}
	s.ExitLight->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
}

// One of the three mode 7 sinks (Mode7SinkControl1-3).
void Mode7SinkBody(TLight* light, int bit, MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField != 7)
	{
		v_lite547->MessageField = v_lite547->MessageField | bit;
		light->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		Mode7SinkBank(caller);
		return;
	}
	if (light->FlasherOnFlag)
		RandomMode7Award();
	else
		tabb08wave->Play(nullptr, "Mode7SinkControl");
	ReleaseHoldSink(-1.0f);
}
}

namespace pirates {
// FT 0x4236a0. Mode 2 keeps the ball held; otherwise one draw picks which hideaway sink releases it.
void HideawayExit()
{
	if (info_text_box->MessageField == 2)
	{
		ReleaseHoldSink(-1.0f);
		return;
	}
	switch (ft::RandIndex(3))
	{
	case 0:
		a_sink17->Message(MessageCode::TSinkResetTimer, -1.0f);
		break;
	case 1:
		a_sink15->Message(MessageCode::TSinkResetTimer, -1.0f);
		break;
	default:
		a_sink14->Message(MessageCode::TSinkResetTimer, -1.0f);
		break;
	}
}

// FT 0x423720. Takes and returns the chained text display time (an argument the listing drops).
float AwardCoins(float time)
{
	coin_light_group->Message(MessageCode::TLightGroupResetAndTurnOn, 1.0f);
	const int count = coin_light_group->Message(MessageCode::TLightGroupGetOnCount, 1.0f);
	if (count < 0 || count > 6)
		Fatal("AwardCoins: coin light group count outside the 7-entry ladder");
	char buf[64];
	const int added = ScoreValue(nullptr, CoinLadder[count]);
	const char* format = table_text::Get(3003);
	if (!format)
		Fatal("AwardCoins: table text 3003 missing from the EXE");
	std::snprintf(buf, sizeof buf, format, static_cast<long>(added));
	const double step = S().BatchFlag ? 0.25 : 2.0; // short texts while a buried-treasure batch runs (0x43abac)
	time = static_cast<float>(step + static_cast<double>(time));
	Show(CoinGlyph, time, 1);
	time = static_cast<float>(step + static_cast<double>(time));
	Show(buf, time, 0);
	tabb84wave->Play(nullptr, "AwardCoins");
	return time;
}

// FT 0x423870. on != 0: clear the bit field and flash all four lights, then stay off; 0: turn them off.
void SetHideawayLights(int on)
{
	const MessageCode code = on ? MessageCode::TLightFlasherStartTimedThenStayOff : MessageCode::TLightResetAndTurnOff;
	const float value = on ? 1.0f : 0.0f;
	if (on)
		v_lite524->MessageField = 0;
	v_lite524->Message(code, value);
	v_lite525->Message(code, value);
	v_lite526->Message(code, value);
	v_lite527->Message(code, value);
}

// FT 0x423900. The four lights follow the low four bits of v_lite524->MessageField (re-read before each light, as the original does).
void RestoreHideawayLights()
{
	TLight* const lights[4] = {v_lite524, v_lite525, v_lite526, v_lite527};
	for (int i = 0; i < 4; i++)
	{
		const bool lit = (v_lite524->MessageField & (1 << i)) != 0;
		lights[i]->Message(lit ? MessageCode::TLightResetAndTurnOn : MessageCode::TLightResetAndTurnOff, 0.0f);
	}
}

// FT 0x4239a0.
float CheckHideawayComplete(float time)
{
	if ((v_lite524->MessageField & 0xf) == 0xf)
	{
		SetHideawayLights(1);
		time = AwardCoins(time);
	}
	return time;
}

// FT 0x428900. Called by the three mode 7 sinks with the sink as caller.
void Mode7SinkBank(TPinballComponent* caller)
{
	const int bits = v_lite547->MessageField;
	if (bits < 0 || bits > 7)
		Fatal("Mode7SinkBank: v_lite547 bit field outside 0..7");
	Show(Mode7SinkGlyph[bits], 2.0f, 1);
	const bool complete = (bits & 7) == 7;
	if (complete)
	{
		v_lite547->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite546->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite545->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite547->MessageField = 0;
	}
	const int added = ScorePart(caller, complete ? 1 : 0);
	tabb08wave->Play(nullptr, "Mode7SinkBank");
	ShowScoreText(3003, added, 4.0f);
	a_blocc->Message(MessageCode::TBlockerSetFrame, -1.0f);
	a_blocf->Message(MessageCode::TBlockerSetFrame, -1.0f);
	a_blocg->Message(MessageCode::TBlockerSetFrame, -1.0f);
	ReleaseHoldSink(-1.0f);
}

// FT 0x427b90 (a_sink17), 0x427e50 (a_sink15), 0x428120 (a_sink14).
void HideawaySinkControl1(MessageCode code, TPinballComponent* caller)
{
	static const HideawaySink s = {v_lite519, v_lite524, v_mlite44, 1, false, true};
	HideawaySinkBody(s, code, caller);
}
void HideawaySinkControl2(MessageCode code, TPinballComponent* caller)
{
	static const HideawaySink s = {v_lite520, v_lite525, v_mlite107, 2, true, false};
	HideawaySinkBody(s, code, caller);
}
void HideawaySinkControl3(MessageCode code, TPinballComponent* caller)
{
	static const HideawaySink s = {v_lite521, v_lite526, v_mlite106, 4, true, false};
	HideawaySinkBody(s, code, caller);
}

// FT 0x428a20 (a_sink5), 0x428a90 (a_sink6), 0x428b00 (a_sink7).
void Mode7SinkControl1(MessageCode code, TPinballComponent* caller) { Mode7SinkBody(v_lite547, 1, code, caller); }
void Mode7SinkControl2(MessageCode code, TPinballComponent* caller) { Mode7SinkBody(v_lite546, 2, code, caller); }
void Mode7SinkControl3(MessageCode code, TPinballComponent* caller) { Mode7SinkBody(v_lite545, 4, code, caller); }
}
