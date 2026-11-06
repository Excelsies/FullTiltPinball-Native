#include "pch.h"
#include "rules/pirates_rules.h"
#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPopupTarget.h"
#include "TSound.h"
#include "TTextBox.h"

using namespace pirates;

// Skulduggery rules: the three drop-target banks (LDUGG, SKU, ERY), their reset helpers and
// the Skulduggery letter lights. Ported from port_listing_pirates.c (FT 0x423b70, 0x423c90, 0x424440, 0x4244a0, 0x424f70,
// 0x4252d0, 0x4264f0, 0x426a40, 0x426eb0).
// The banks receive the part that was hit (the control function's second argument). Ghidra lost it in the listing's
// ScorePart / ScoreValue calls (shown as a stack artefact); the machine code passes `caller` there.
namespace {
// MessageField masks of the FT originals: the per-bank hit bits live in the low bits of the drop target's MessageField.
const int LduggAll = 0x1f, SkuAll = 0x07, EryAll = 0x07;

bool BankComplete(TPopupTarget* first, int mask) { return (first->MessageField & mask) == mask; }

// The six sends every completed bank makes to the two blockers and the two lights (FT 0x42651x-0x426583 and its twins):
// a_blocc, a_bloce, v_lite504, v_lite505, a_blocf, a_blocg. Only the frames of a_blocc / a_blocf differ per bank.
void BankCompleteLights(float blocFrameC, float blocFrameF)
{
	a_blocc->Message(MessageCode::TBlockerSetFrame, blocFrameC);
	a_bloce->Message(MessageCode::TBlockerSetFrame, 1.0f);
	v_lite504->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	v_lite505->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_blocf->Message(MessageCode::TBlockerSetFrame, blocFrameF);
	a_blocg->Message(MessageCode::TBlockerSetFrame, 0.0f);
}
}

namespace pirates {

void SetSkulduggeryLights(int param_1)
{
	// FT 0x423b70: code 0x10 (flasher then off) with 1.0 when param_1 != 0, else 0x14 (reset and off) with 0.0
	const float value = (param_1 != 0) ? 1.0f : 0.0f;
	const MessageCode code = (param_1 != 0) ? MessageCode::TLightFlasherStartTimedThenStayOff : MessageCode::TLightResetAndTurnOff;
	if (param_1 != 0)
		v_lite1->MessageField = 0;
	v_lite1->Message(code, value);
	v_lite2->Message(code, value);
	v_lite3->Message(code, value);
	v_lite4->Message(code, value);
	v_lite5->Message(code, value);
	v_lite6->Message(code, value);
	v_lite7->Message(code, value);
	v_lite8->Message(code, value);
	v_lite9->Message(code, value);
	v_lite10->Message(code, value);
	v_lite11->Message(code, value);
}

namespace {
// One letter of a bank (FT 0x423c90 repeats this block eleven times): the letter's light and its drop target follow the
// bit of v_lite1->MessageField; a lit letter disables the target and sets the bank's hit bit `hitBit` in the first target.
void RestoreLetter(unsigned litBit, TLight* light, TPopupTarget* target, TPopupTarget* first, int hitBit)
{
	if ((v_lite1->MessageField & litBit) == 0)
	{
		light->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		target->Message(MessageCode::TPopupTargetEnable, 0.0f);
	}
	else
	{
		light->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		target->Message(MessageCode::TPopupTargetDisable, 0.0f);
		first->MessageField = first->MessageField | hitBit;
	}
}
}

void RestoreSkulduggeryTargets()
{
	// SKU bank: v_lite1..3 / a_drop13..15, bits 0..2 of the letter word
	a_drop13->MessageField = 0;
	if ((v_lite1->MessageField & 7) == 7)
	{
		v_lite1->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_drop13->Message(MessageCode::TPopupTargetEnable, 0.0f);
		v_lite2->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_drop14->Message(MessageCode::TPopupTargetEnable, 0.0f);
		v_lite3->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_drop15->Message(MessageCode::TPopupTargetEnable, 0.0f);
	}
	else
	{
		RestoreLetter(0x01, v_lite1, a_drop13, a_drop13, 1);
		RestoreLetter(0x02, v_lite2, a_drop14, a_drop13, 2);
		RestoreLetter(0x04, v_lite3, a_drop15, a_drop13, 4);
	}

	// LDUGG bank: v_lite4..8 / a_drop5..9, bits 3..7
	a_drop5->MessageField = 0;
	if ((v_lite1->MessageField & 0xf8) == 0xf8)
	{
		v_lite4->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_drop5->Message(MessageCode::TPopupTargetEnable, 0.0f);
		v_lite5->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_drop6->Message(MessageCode::TPopupTargetEnable, 0.0f);
		v_lite6->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_drop7->Message(MessageCode::TPopupTargetEnable, 0.0f);
		v_lite7->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_drop8->Message(MessageCode::TPopupTargetEnable, 0.0f);
		v_lite8->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_drop9->Message(MessageCode::TPopupTargetEnable, 0.0f);
	}
	else
	{
		RestoreLetter(0x08, v_lite4, a_drop5, a_drop5, 1);
		RestoreLetter(0x10, v_lite5, a_drop6, a_drop5, 2);
		RestoreLetter(0x20, v_lite6, a_drop7, a_drop5, 4);
		RestoreLetter(0x40, v_lite7, a_drop8, a_drop5, 8);
		RestoreLetter(0x80, v_lite8, a_drop9, a_drop5, 0x10);
	}

	// ERY bank: v_lite9..11 / a_drop10..12, bits 8..10
	a_drop10->MessageField = 0;
	if (((v_lite1->MessageField >> 8) & 7) == 7)
	{
		v_lite9->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_drop10->Message(MessageCode::TPopupTargetEnable, 0.0f);
		v_lite10->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_drop11->Message(MessageCode::TPopupTargetEnable, 0.0f);
		v_lite11->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_drop12->Message(MessageCode::TPopupTargetEnable, 0.0f);
	}
	else
	{
		RestoreLetter(0x100, v_lite9, a_drop10, a_drop10, 1);
		RestoreLetter(0x200, v_lite10, a_drop11, a_drop10, 2);
		RestoreLetter(0x400, v_lite11, a_drop12, a_drop10, 4);
	}
}

void ResetLduggTargets()
{
	a_drop5->MessageField = 0;
	a_drop5->Message(MessageCode::TPopupTargetEnable, 0.0f);
	a_drop6->Message(MessageCode::TPopupTargetEnable, 0.0f);
	a_drop7->Message(MessageCode::TPopupTargetEnable, 0.0f);
	a_drop8->Message(MessageCode::TPopupTargetEnable, 0.0f);
	a_drop9->Message(MessageCode::TPopupTargetEnable, 0.0f);
}

void DropLduggTargets()
{
	a_drop5->MessageField = 0x1f;
	a_drop5->Message(MessageCode::TPopupTargetDisable, 0.0f);
	a_drop6->Message(MessageCode::TPopupTargetDisable, 0.0f);
	a_drop7->Message(MessageCode::TPopupTargetDisable, 0.0f);
	a_drop8->Message(MessageCode::TPopupTargetDisable, 0.0f);
	a_drop9->Message(MessageCode::TPopupTargetDisable, 0.0f);
}

void ResetSkuTargets()
{
	a_drop13->MessageField = 0;
	a_drop13->Message(MessageCode::TPopupTargetEnable, 0.0f);
	a_drop14->Message(MessageCode::TPopupTargetEnable, 0.0f);
	a_drop15->Message(MessageCode::TPopupTargetEnable, 0.0f);
}

void ResetEryTargets()
{
	a_drop10->MessageField = 0;
	a_drop10->Message(MessageCode::TPopupTargetEnable, 0.0f);
	a_drop11->Message(MessageCode::TPopupTargetEnable, 0.0f);
	a_drop12->Message(MessageCode::TPopupTargetEnable, 0.0f);
}

void LduggTargetBank(TPinballComponent* caller)
{
	// FT 0x4264f0. Modes 2 and 12 first run the Skulduggery completion check; a hit (non-zero) ends the call. Modes 4 and 6
	// have their own bodies; every other mode (3, 5, 8-11 and anything outside 2..12) and the mode 7 body is the default.
	const int mode = info_text_box->MessageField;
	if (mode == 4)
	{
		if (BankComplete(a_drop5, LduggAll))
		{
			ResetLduggTargets();
			const float time = AwardJackpot(0.0f);
			fathom_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, time);
			ScoreValue(caller, 25000);
			return;
		}
		tabb22wave->Play(nullptr, "LduggTargetBank");
		ScoreValue(caller, 10000);
		return;
	}
	if (mode == 6)
	{
		if (BankComplete(a_drop5, LduggAll))
			ResetLduggTargets();
		return;
	}
	if (mode == 2 && CheckSkulduggeryStart() != 0)
		return;
	if (mode == 12 && CheckSkulduggeryLava() != 0)
		return;
	if (BankComplete(a_drop5, LduggAll))
	{
		BankCompleteLights(0.0f, -1.0f);
		ResetLduggTargets();
		ScorePart(caller, 1);
		tabb24wave->Play(nullptr, "LduggTargetBank");
		return;
	}
	ScorePart(caller, 0);
	tabb22wave->Play(nullptr, "LduggTargetBank");
}

void SkuTargetBank(TPinballComponent* caller)
{
	// FT 0x426a40: modes 2 / 12 check first, 6 resets a full bank, 7 awards magnets and jackpots, the rest is the default
	const int mode = info_text_box->MessageField;
	if (mode == 6)
	{
		if (BankComplete(a_drop13, SkuAll))
			ResetSkuTargets();
		return;
	}
	if (mode == 7)
	{
		if (BankComplete(a_drop13, SkuAll))
		{
			BankCompleteLights(0.0f, 0.0f);
			ResetSkuTargets();
			const float time = ActivateRandomMagnet(0.0f);
			AwardJackpot(time);
			ScoreValue(nullptr, 50000);
			return;
		}
		ActivateRandomMagnet(0.0f);
		tabb87wave->Play(nullptr, "SkuTargetBank");
		ScoreValue(nullptr, 25000);
		return;
	}
	if (mode == 2 && CheckSkulduggeryStart() != 0)
		return;
	if (mode == 12 && CheckSkulduggeryLava() != 0)
		return;
	if (BankComplete(a_drop13, SkuAll))
	{
		BankCompleteLights(0.0f, 0.0f);
		ResetSkuTargets();
		ScorePart(caller, 1);
		tabb29wave->Play(nullptr, "SkuTargetBank");
		return;
	}
	ScorePart(caller, 0);
	tabb02wave->Play(nullptr, "SkuTargetBank");
}

void EryTargetBank(TPinballComponent* caller)
{
	// FT 0x426eb0: modes 2 / 12 check first, 6 resets a full bank, 7 scores without the blockers, 10 is the mode 10
	// branch, the rest is the default (the sound plays before ScorePart when the bank is not complete)
	const int mode = info_text_box->MessageField;
	if (mode == 6)
	{
		if (BankComplete(a_drop10, EryAll))
			ResetEryTargets();
		return;
	}
	if (mode == 7)
	{
		if (BankComplete(a_drop10, EryAll))
		{
			ResetEryTargets();
			ScorePart(caller, 1);
			tabb19wave->Play(nullptr, "EryTargetBank");
			return;
		}
		tabb07wave->Play(nullptr, "EryTargetBank");
		ScorePart(caller, 0);
		return;
	}
	if (mode == 10)
	{
		if (BankComplete(a_drop10, EryAll))
		{
			State& st = S();
			if ((st.ModeCounter & 0x70) == 0)
			{
				st.ModeCounter |= 0x70;
				v_lite9->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
				v_lite10->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
				v_lite11->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
			}
			else
			{
				v_lite9->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
				v_lite10->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
				v_lite11->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			}
			ResetEryTargets();
			CheckMode10Banks(0.0f);
		}
		else
		{
			ScoreValue(nullptr, 30000);
		}
		tabb94wave->Play(nullptr, "EryTargetBank");
		return;
	}
	if (mode == 2 && CheckSkulduggeryStart() != 0)
		return;
	if (mode == 12 && CheckSkulduggeryLava() != 0)
		return;
	if (BankComplete(a_drop10, EryAll))
	{
		BankCompleteLights(-1.0f, 0.0f);
		ResetEryTargets();
		ScorePart(caller, 1);
		tabb19wave->Play(nullptr, "EryTargetBank");
		return;
	}
	tabb07wave->Play(nullptr, "EryTargetBank");
	ScorePart(caller, 0);
}

}
