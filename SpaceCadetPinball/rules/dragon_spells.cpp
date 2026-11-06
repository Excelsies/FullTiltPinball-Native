#include "pch.h"
#include "rules/dragon_rules.h"
#include "TBlocker.h"
#include "TKickout.h"
#include "TLight.h"
#include "TSink.h"
#include "TSound.h"
#include "TTextBox.h"
#include <cstdio>

// Dragon's Keep spell rules (port of DRAGON.EXE 0x423600 PickUnlitSpell, 0x4236b0 AwardSpell, 0x423780
// ResetSpells, 0x4237f0 AwardRandomSpell and the spell target bank 0x424d20 with its five control functions 0x424e60 ..
// 0x425060). Spell index 0..3 = lights v_lite28 / v_lite29 / v_lite31 / v_lite32 (the "wizard" letters), counted in
// DragonState::Spells (0x434d24, the count of bonus line 3015).
namespace dragon {
namespace {
// info_text_box MessageField values the bank branches on (mode numbers of MissionControlSwitch).
const int ModeWaitQuest = 2;
const int Mode4Num = 4;
const int Mode8Num = 8;

TLight* const* SpellLights(int& count)
{
	static TLight* lights[4];
	lights[0] = v_lite28;
	lights[1] = v_lite29;
	lights[2] = v_lite31;
	lights[3] = v_lite32;
	count = 4;
	return lights;
}

// The bank of five spell lights (v_lite3..v_lite7), lit by the five targets.
bool SpellBankComplete()
{
	return v_lite3->light_on() && v_lite4->light_on() && v_lite5->light_on() && v_lite6->light_on() && v_lite7->light_on();
}

void SpellTargetBank(TPinballComponent* caller)
{
	if (!SpellBankComplete())
	{
		if (info_text_box->MessageField != Mode4Num)
		{
			ScorePart(caller, 0);
			tabc05wave->Play(nullptr, "SpellTargetBank");
		}
		return;
	}
	v_lite3->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	v_lite4->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	v_lite5->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	v_lite6->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	v_lite7->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	if (info_text_box->MessageField == ModeWaitQuest)
	{
		v_lite23->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		if (CheckMode15Start() != 0)
			return;
	}
	AwardRandomSpell();
}

// One spell target: index = position in the bank (0..4), light = its bank light, textId = the mode 8 string.
void SpellTarget(TPinballComponent* caller, MessageCode code, int index, TLight* light, int textId, const char* what)
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField == Mode8Num)
	{
		a_sink4->MessageField = index;
		tabc05wave->Play(nullptr, what);
		const char* text = R().GetText(textId);
		if (text)
			info_text_box->Display(text, 2.0f, 0, false);
		else
			std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), %s shows nothing\n", textId, what);
		return;
	}
	light->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	SpellTargetBank(caller);
}
}

int PickUnlitSpell()
{
	int count = 0;
	TLight* const* lights = SpellLights(count);
	int unlit[4];
	int unlitCount = 0;
	for (int i = 0; i < count; ++i)
	{
		if (!lights[i]->light_on())
			unlit[unlitCount++] = i;
	}
	if (unlitCount == 0)
		return -1;
	return unlit[ft::RandIndex(unlitCount)];
}

void AwardSpell(int n)
{
	S().Spells += 1;
	switch (n)
	{
	case 0:
		v_lite28->Message(MessageCode::TLightTurnOnTimed, 30.0f);
		break;
	case 1:
		v_lite29->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite30->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		a_kout3->ActiveFlag = 1;
		break;
	case 2:
		v_lite31->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		break;
	default:
		BlockerFControl(MessageCode::TBlockerDisable, a_blocf);
		break;
	}
	ScoreValue(nullptr, 120000);
	tabc35wave->Play(nullptr, "AwardSpell");
	info_text_box->Display("$", 2.0f, 1, false);   // the spell glyph (symbol font 1; a one-character code constant, not table text)
}

void ResetSpells()
{
	v_lite28->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite29->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite30->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_kout3->Message(MessageCode::Reset, 0.0f);
	v_lite31->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	BlockerFControl(MessageCode::TBlockerEnable, a_blocf);
}

void AwardRandomSpell()
{
	const int spell = PickUnlitSpell();
	if (spell != -1)
		AwardSpell(spell);
}

void SpellTargetControl1(MessageCode code, TPinballComponent* caller) { SpellTarget(caller, code, 0, v_lite3, 3039, "SpellTargetControl1"); }
void SpellTargetControl2(MessageCode code, TPinballComponent* caller) { SpellTarget(caller, code, 1, v_lite4, 3040, "SpellTargetControl2"); }
void SpellTargetControl3(MessageCode code, TPinballComponent* caller) { SpellTarget(caller, code, 2, v_lite5, 3041, "SpellTargetControl3"); }
void SpellTargetControl4(MessageCode code, TPinballComponent* caller) { SpellTarget(caller, code, 3, v_lite6, 3042, "SpellTargetControl4"); }
void SpellTargetControl5(MessageCode code, TPinballComponent* caller) { SpellTarget(caller, code, 4, v_lite7, 3043, "SpellTargetControl5"); }
}
