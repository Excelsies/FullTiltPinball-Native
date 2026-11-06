#include "pch.h"
#include "rules/pirates_rules.h"
#include "table_text.h"
#include <cstdio>
#include <cstdlib>
#include "TBlocker.h"
#include "TBumper.h"
#include "TLight.h"
#include "TPopupTarget.h"
#include "TSound.h"
#include "TTextBox.h"

using namespace pirates;

// Skulduggery rules: the three bumpers, the 11 Skulduggery drop-target handlers (LDUGG 5,
// SKU 3, ERY 3 with their letter bits), the two blocker-bank targets and the blocker bank. Ported from port_listing_pirates.c
// (FT 0x426390, 0x4268b0-0x4269f0, 0x426dc0-0x426e60, 0x427260-0x427300, 0x4278c0-0x427980) and re-read against the machine code.
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// crt_sprintf(buf, get_rc_string(id), value) + TTextBox::Display(buf, 2.0, 0, 0) (FT 0x426428-0x426444). The format is the
// EXE's own string, not a literal; the long cast satisfies %ld and %d alike on x86-64.
void ShowWithValue(int stringId, int value, float time)
{
	const char* format = table_text::Get(stringId);
	if (!format)
		Fatal("drop targets: table text id missing from the EXE");
	char buf[64]; // the original's 64-byte buffer
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
	std::snprintf(buf, sizeof buf, format, static_cast<long>(value));
#pragma GCC diagnostic pop
	info_text_box->Display(buf, time, 0, false);
}

// The ten-line body every letter handler shares (FT 0x4268b0 and its eleven twins): `bank` first receives its hit bit in the
// bank's first target; in modes 2 and 12 (the Skulduggery letter modes) the letter bit goes into v_lite1's MessageField and the
// letter's light flashes; then the bank runs with the calling component. `flashLight` is v_lite1 itself for the S of SKU.
void LetterTargetHit(TPopupTarget* first, int hitBit, unsigned letterBit, TLight* flashLight, TPinballComponent* caller,
                     void (*bank)(TPinballComponent*))
{
	first->MessageField = first->MessageField | hitBit;
	const int mode = info_text_box->MessageField;
	if (mode == 2 || mode == 0xc)
	{
		v_lite1->MessageField = v_lite1->MessageField | letterBit;
		flashLight->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	bank(caller);
}

// The two blocker-bank targets (FT 0x427940 / 0x427980): their bit in v_lite522's MessageField, the bit's light flashes, then the bank.
void BlockerTargetHit(int bit, TLight* flashLight, TPinballComponent* caller)
{
	v_lite522->MessageField = v_lite522->MessageField | bit;
	flashLight->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	BlockerTargetBank(caller);
}
}

namespace pirates {

// 0x426390 (a_bump1-3). Mode 2: a_bump1 counts the hits; the 12th resets it, raises the crew a_bump2 (max 6), shows text 3001 with
// the new crew's score and plays tabb17 (tabb16 for the other hits); the hit scores ScorePart(bumper, crew). Mode 9:
// the shared mode counter counts to 10, the 10th hit awards the jackpot; every other hit scores 25,000. Any other mode scores
// the crew's value. The listing's text value is one crew too high (a stack-simulation slip): the machine code at FT 0x426406-0x426412
// increments the crew, stores it and passes that new crew to a single scoring call, so the text shows the score of the new crew.
void BumperControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	const int mode = info_text_box->MessageField;
	if (mode == 2)
	{
		const int hits = a_bump1->MessageField + 1;
		if (hits == 0xc)
		{
			a_bump1->MessageField = 0;
			if (a_bump2->MessageField < 6)
			{
				a_bump2->MessageField = a_bump2->MessageField + 1;
				ShowWithValue(3001, caller->get_scoring(a_bump2->MessageField), 2.0f);
			}
			tabb17wave->Play(nullptr, "BumperControl");
		}
		else
		{
			tabb16wave->Play(nullptr, "BumperControl");
			a_bump1->MessageField = hits;
		}
		ScorePart(caller, a_bump2->MessageField);
		return;
	}
	if (mode != 9)
	{
		tabb16wave->Play(nullptr, "BumperControl");
		ScorePart(caller, a_bump2->MessageField);
		return;
	}
	State& st = S();
	st.ModeCounter = st.ModeCounter + 1;
	if (st.ModeCounter == 10)
	{
		st.ModeCounter = 0;
		AwardJackpot(0.0f);
		return;
	}
	ScoreValue(caller, 25000);
	tabb74wave->Play(nullptr, "BumperControl");
}

// LDUGG letters: a_drop5..9 carry bits 1..0x10 in a_drop5, letter bits 0x08..0x80 in v_lite1 (lights v_lite4..8)
void LduggTargetControl1(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LetterTargetHit(a_drop5, 0x01, 0x08, v_lite4, caller, LduggTargetBank);
}

void LduggTargetControl2(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LetterTargetHit(a_drop5, 0x02, 0x10, v_lite5, caller, LduggTargetBank);
}

void LduggTargetControl3(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LetterTargetHit(a_drop5, 0x04, 0x20, v_lite6, caller, LduggTargetBank);
}

void LduggTargetControl4(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LetterTargetHit(a_drop5, 0x08, 0x40, v_lite7, caller, LduggTargetBank);
}

void LduggTargetControl5(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LetterTargetHit(a_drop5, 0x10, 0x80, v_lite8, caller, LduggTargetBank);
}

// SKU letters: a_drop13..15 carry bits 1..4 in a_drop13, letter bits 0x01..0x04 in v_lite1 (lights v_lite1..3)
void SkuTargetControl1(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LetterTargetHit(a_drop13, 0x01, 0x01, v_lite1, caller, SkuTargetBank);
}

void SkuTargetControl2(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LetterTargetHit(a_drop13, 0x02, 0x02, v_lite2, caller, SkuTargetBank);
}

void SkuTargetControl3(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LetterTargetHit(a_drop13, 0x04, 0x04, v_lite3, caller, SkuTargetBank);
}

// ERY letters: a_drop10..12 carry bits 1..4 in a_drop10, letter bits 0x100..0x400 in v_lite1 (lights v_lite9..11)
void EryTargetControl1(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LetterTargetHit(a_drop10, 0x01, 0x100, v_lite9, caller, EryTargetBank);
}

void EryTargetControl2(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LetterTargetHit(a_drop10, 0x02, 0x200, v_lite10, caller, EryTargetBank);
}

void EryTargetControl3(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LetterTargetHit(a_drop10, 0x04, 0x400, v_lite11, caller, EryTargetBank);
}

// 0x4278c0. Both bits of v_lite522 set: the blocker a_blocf drops to its frame -1, the bits clear and both lights flash off; the
// hit scores ScorePart(caller, complete) (the listing's `unaff_retaddr` is the caller), then tabb02 plays.
void BlockerTargetBank(TPinballComponent* caller)
{
	const bool complete = (v_lite522->MessageField & 3) == 3;
	if (complete)
	{
		a_blocf->Message(MessageCode::TBlockerSetFrame, -1.0f);
		v_lite522->MessageField = 0;
		v_lite522->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite779->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	}
	ScorePart(caller, complete ? 1 : 0);
	tabb02wave->Play(nullptr, "BlockerTargetBank");
}

// 0x427940 (a_spot4): bit 1, light v_lite522
void BlockerTargetControl1(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		BlockerTargetHit(1, v_lite522, caller);
}

// 0x427980 (a_spot5): bit 2, light v_lite779
void BlockerTargetControl2(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		BlockerTargetHit(2, v_lite779, caller);
}

}
