#include "pch.h"
#include "rules/dragon_rules.h"
#include "TBlocker.h"
#include "TKickout.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSink.h"
#include "TSoloTarget.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "table_text.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Dragon's Keep quest selection: the sword (SetSwordState and its three targets), the random quest pick,
// starting and completing the selected quest, the quest-select target, the two lizard sinks (a_sink1, a_sink2) with their shared
// bank, and the gem bitmask. Port of DRAGON.EXE 0x4238d0, 0x423e50, 0x423e90, 0x424000, 0x424210, 0x425650,
// 0x425750, 0x4257c0, 0x425830, 0x425c30, 0x425ea0, 0x426000, 0x428a20 (re/work/spec/port_listing_dragon.c; the
// machine code was read where the listing lost an argument or a receiver: LizardSinkBank, the sink controls).
namespace dragon {
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

const MessageCode MissionComplete = MessageCode::ControlMissionComplete;

// Quest lights v_lite71..75 (table at 0x43bb58): quest 4 + i is selected with light i.
TLight* QuestLight(int i)
{
	switch (i)
	{
	case 0: return v_lite71;
	case 1: return v_lite72;
	case 2: return v_lite73;
	case 3: return v_lite74;
	case 4: return v_lite75;
	default: Fatal("quest light index out of range");
	}
}

// Shows string resource `id` on the info box (the original's get_rc_string + TTextBox::Display). No substitute text.
void ShowText(int id, float time)
{
	const char* text = table_text::Get(id);
	if (!text)
	{
		std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), nothing shown\n", id);
		return;
	}
	info_text_box->Display(text, time, 0, false);
}

// Glyph strings (0x439a58 "!") are drawn with font slot 1.
void ShowGlyph(const char* glyph, float time)
{
	info_text_box->Display(glyph, time, 1, false);
}

// ScoreValue's result in the original is AddScore's (ScoreAdded + score * multiplier, TPinballTable::AddScore). The shared
// dragon::ScoreValue returns nothing, so the same sum is taken before the call.
int ScoreValueResult(TPinballComponent* comp, int score)
{
	TPinballTable* table = control::TableG;
	const int added = table->ScoreAdded + score * table->ScoreMultiplier;
	ScoreValue(comp, score);
	return added;
}

// crt_sprintf(buf, get_rc_string(id), value) for the one-number formats ("%ld" / "%d") of the quest texts.
void FormatNumber(const char* fmt, int value, char* out, size_t size)
{
	const char* pct = std::strchr(fmt, '%');
	if (!pct)
		Fatal("quest text format without a conversion");
	const char* tail = pct + 1;
	if (*tail == 'l')
		++tail;
	if (*tail != 'd')
		Fatal("quest text format is not %d / %ld");
	++tail;
	std::snprintf(out, size, "%.*s%d%s", static_cast<int>(pct - fmt), fmt, value, tail);
}

// LizardSinkBank 0x425c30: both sinks' common part. `caller` is the sink that was hit.
void LizardSinkBank(TPinballComponent* caller)
{
	const int mode = Mode();
	if (mode == 2)
	{
		v_lite63->MessageField++;
		if (v_lite63->MessageField < 2)
		{
			tabc16wave->Play(nullptr, "LizardSinkBank");
			if (caller != a_sink1)
				a_sink1->Message(MessageCode::TSinkResetTimer, -1.0f);
			else
				a_sink2->Message(MessageCode::TSinkResetTimer, -1.0f);
			return;
		}
		v_lite63->MessageField = 0;
		v_lite22->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		S().BallHeld = 1;
		if (CheckMode15Start() != 0)
			return;
		S().BallHeld = 0;
		if (StartSelectedQuest() != 0)
			return;
		a_sink1->Message(MessageCode::TSinkResetTimer, -1.0f);
		return;
	}

	tabc16wave->Play(nullptr, "LizardSinkBank");
	TLight* light = (caller == a_sink1) ? static_cast<TLight*>(v_lite65) : static_cast<TLight*>(v_lite66);
	if (caller != a_sink1)
		a_sink1->Message(MessageCode::TSinkResetTimer, -1.0f);
	else
		a_sink2->Message(MessageCode::TSinkResetTimer, -1.0f);
	light->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
	if (mode != 5)
		return;

	if (v_lite91->MessageField < 10)
		v_lite91->MessageField++;
	int step = a_sink2->MessageField;
	if (step < 0 || step > 2)
	{
		AwardJackpot(0.0f, 1);
		step = 0;
		v_lite91->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	else
	{
		if (step == 2)
			v_lite91->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		step++;
		const int awarded = ScoreValueResult(caller, v_lite91->MessageField * 100000);
		const char* fmt = table_text::Get(3029);
		if (!fmt)
			std::fprintf(stderr, "FT rules: text 3029 unavailable (EXE missing or string absent), nothing shown\n");
		else
		{
			char text[80]; // the original's 0x40-byte buffer
			FormatNumber(fmt, awarded, text, sizeof text);
			info_text_box->Display(text, 2.0f, 0, false);
		}
	}
	a_sink2->MessageField = step;
}

// The sink handlers of both sinks (0x425ea0, 0x426000) differ only in which of v_lite63/64/65/66 they use.
void LizardSink(TPinballComponent* caller, TLight* ownLight, TLight* flashLight, bool second)
{
	const int mode = Mode();
	if (mode == 3)
	{
		v_lite63->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite64->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		if (!second) // sink 1 sends the pair twice (0x425f37..0x425f71); sink 2 once (0x426097..0x4260b3)
		{
			v_lite63->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			v_lite64->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		}
		SetMode(mode_timer->MessageField);
		MissionControl(static_cast<int>(MessageCode::ControlModeResume), nullptr);
		mode_timer->MessageField = -1;
		caller->Message(MessageCode::TSinkResetTimer, -1.0f);
		return;
	}
	const float time = static_cast<float>(static_cast<int>(a_sink9->TimerTime));
	if (mode != 5)
	{
		ownLight->Message(MessageCode::TLightTurnOn, 0.0f);
		ownLight->Message(MessageCode::TLightFlasherStartTimed, time);
	}
	flashLight->Message(MessageCode::TLightFlasherStartTimed, time);
	ScorePart(caller, 0);
	LizardSinkBank(caller);
}
}

// 0x4238d0: sword state -1 (hidden, all quests done), 0..3.
void SetSwordState(int state)
{
	if (state < -1 || state > 3)
		return; // the jump table ends in the default branch: nothing
	if (state == 0 || state == 1 || state == 2)
		tabc13wave->Play(nullptr, "SetSwordState");
	switch (state)
	{
	case -1:
		a_sword->Message(MessageCode::TBlockerMoveToFrame, 0.0f);
		a_spot19->Message(MessageCode::TSoloTargetDisable, 0.0f);
		a_spot26->Message(MessageCode::TSoloTargetDisable, 0.0f);
		a_spot28->Message(MessageCode::TSoloTargetDisable, 0.0f);
		v_lite16->Message(MessageCode::TLightResetAndTurnOff, -1.0f);
		v_lite17->Message(MessageCode::TLightResetAndTurnOff, -1.0f);
		v_lite18->Message(MessageCode::TLightResetAndTurnOff, -1.0f);
		break;
	case 0:
		a_sword->Message(MessageCode::TBlockerMoveToFrame, 0.0f);
		a_spot19->Message(MessageCode::TSoloTargetDisable, 0.0f);
		a_spot26->Message(MessageCode::TSoloTargetDisable, 0.0f);
		a_spot28->Message(MessageCode::TSoloTargetEnable, 0.0f);
		v_lite16->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite17->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite18->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		break;
	case 1:
		a_sword->Message(MessageCode::TBlockerMoveToFrame, 0.0f);
		a_spot19->Message(MessageCode::TSoloTargetDisable, 0.0f);
		a_spot26->Message(MessageCode::TSoloTargetDisable, 0.0f);
		a_spot28->Message(MessageCode::TSoloTargetEnable, 0.0f);
		v_lite16->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		v_lite17->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		v_lite18->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		break;
	case 2:
		a_sword->Message(MessageCode::TBlockerMoveToFrame, 1.0f);
		a_spot19->Message(MessageCode::TSoloTargetDisable, 0.0f);
		a_spot26->Message(MessageCode::TSoloTargetEnable, 0.0f);
		a_spot28->Message(MessageCode::TSoloTargetDisable, 0.0f);
		v_lite16->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		v_lite17->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite18->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		break;
	case 3:
		a_sword->Message(MessageCode::TBlockerEnable, 0.0f);
		a_spot19->Message(MessageCode::TSoloTargetEnable, 0.0f);
		a_spot26->Message(MessageCode::TSoloTargetDisable, 0.0f);
		a_spot28->Message(MessageCode::TSoloTargetDisable, 0.0f);
		v_lite16->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite17->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite18->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		break;
	}
}

// 0x423e50: stops the flashing light of the selected quest and forgets the selection.
void CancelQuestSelection()
{
	if (a_kout2->MessageField != 0)
	{
		QuestLight(a_rockb->MessageField)->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_kout2->MessageField = 0;
	}
}

// 0x423e90: picks one of the quests whose light is off (index into v_lite71..75); a_kout2 holds the quest (4..8),
// a_rockb the light index. All five done: the sword hides and mode 9 is started. -1 after a drain.
int SelectRandomQuest()
{
	if (S().BallDrained != 0)
		return -1;
	CancelQuestSelection();
	int lightIndex[5];
	int quest[5];
	int count = 0;
	for (int i = 0; i < 5; i++)
	{
		if (!QuestLight(i)->light_on())
		{
			lightIndex[count] = i;
			quest[count] = 4 + i;
			count++;
		}
	}
	if (count == 0)
	{
		SetSwordState(-1);
		v_lite76->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		SetMode(9);
		MissionControl(static_cast<int>(MissionComplete), nullptr);
		return 9;
	}
	const int pick = ft::RandIndex(count);
	a_rockb->MessageField = lightIndex[pick];
	a_kout2->MessageField = quest[pick];
	QuestLight(lightIndex[pick])->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	return quest[pick];
}

// 0x424000: the selected quest is done: +500,000 jackpot, one more quest done, its light stays on; pick the next.
void CompleteQuest()
{
	if (a_kout2->MessageField != 0)
	{
		AddJackpot(500000);
		v_lite28->MessageField++;
		QuestLight(a_rockb->MessageField)->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_kout2->MessageField = 0;
	}
	SelectRandomQuest();
}

// 0x424210: starts the selected quest (its number becomes the mode), 1 when there was one.
int StartSelectedQuest()
{
	if (a_kout2->MessageField == 0)
		return 0;
	SetMode(a_kout2->MessageField);
	v_lite46->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	a_rockb->Message(MessageCode::TBlockerEnable, 2.0f);
	MissionControl(static_cast<int>(MissionComplete), nullptr);
	return 1;
}

// 0x425650: a_spot27, the quest-select target. A lit v_lite57 is spent on an extra ball instead.
void QuestSelectTargetControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	if (v_lite57->light_on())
	{
		v_lite57->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		table_add_extra_ball(5.0f);
		return;
	}
	tabc96wave->Play(nullptr, "QuestSelectTargetControl");
	v_lite21->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
	if (Mode() == 2)
	{
		const int quest = SelectRandomQuest();
		if (quest >= 4 && quest <= 8)
			ShowText(3071 + (quest - 4), 2.0f);
	}
	ScorePart(caller, 0);
}

// 0x425750, 0x4257c0: a_spot19 (flashing v_lite16) and a_spot26 (flashing v_lite17) move the sword on.
void SwordTargetControl1(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision || Mode() != 2 || !v_lite16->FlasherOnFlag)
		return;
	ShowGlyph("!", 2.0f);
	SetSwordState(2);
	dragon_fire_light_group->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	ScorePart(caller, 0);
}

void SwordTargetControl2(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision || Mode() != 2 || !v_lite17->FlasherOnFlag)
		return;
	ShowGlyph("!", 2.0f);
	SetSwordState(1);
	dragon_fire_light_group->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	ScorePart(caller, 0);
}

// 0x425830: a_spot28 with v_lite18 flashing is the shortcut to mode 9 (no score).
void SwordTargetControl3(MessageCode code, TPinballComponent*)
{
	if (code != MessageCode::ControlCollision || Mode() != 2 || !v_lite18->FlasherOnFlag)
		return;
	ShowGlyph("!", 2.0f);
	SetSwordState(-1);
	CancelQuestSelection();
	v_lite76->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	SetMode(9);
	MissionControl(static_cast<int>(MissionComplete), nullptr);
}

// 0x425ea0, 0x426000: a_sink1 / a_sink2.
void LizardSinkControl1(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LizardSink(caller, v_lite63, v_lite66, false);
}

void LizardSinkControl2(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		LizardSink(caller, v_lite64, v_lite65, true);
}

// 0x428a20: gem bits collected in mode 4; all nine gems give an extra ball.
void GemCollected(unsigned bits)
{
	v_lite88->MessageField = static_cast<int>(static_cast<unsigned>(v_lite88->MessageField) | bits);
	if (v_lite88->MessageField == 0x1ff)
	{
		gem_light_group->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		table_add_extra_ball(2.0f);
		return;
	}
	tabc47wave->Play(nullptr, "GemCollected");
}
}
