#include "pch.h"
#include "rules/dragon_rules.h"
#include "TBlocker.h"
#include "TDiverter.h"
#include "TKickout.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TLightSweeps.h"
#include "TOneway.h"
#include "TPinballTable.h"
#include "TRamp.h"
#include "TSink.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "TWall.h"
#include "table_text.h"
#include <cstdio>
#include <cstdlib>

// Dragon's Keep rules: the dragon jaw (CloseDragonJaw 0x424050, OpenDragonJaw 0x424130), the dragon mouth
// sink (DragonMouthSinkControl 0x427f10) and mode 9 (Mode9Controller 0x42a150). Port of
// port_listing_dragon.c lines 952-1006, 3917-4060 and 5073-5397; the Message arguments Ghidra lost were read from the
// machine code (DragonMouthSinkControl: sink resets, timer extension, fire-group count switch).
namespace dragon {
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// Timer extension of FT 0x427f6f: float(double(float(ms)) / 1000.0) + 15.0, rounded to float after each step.
float ExtendedTimerSeconds(int timeLeftMs)
{
	const float ms = static_cast<float>(timeLeftMs);
	const float sec = static_cast<float>(static_cast<double>(ms) / 1000.0);
	return static_cast<float>(static_cast<double>(sec) + 15.0);
}

// sprintf(buf, string id, value) followed by Display(buf, time, 0, 0). A missing string is reported and the display is
// skipped: no replacement text is invented.
void DisplayFormatted(int id, int value, float time)
{
	const char* fmt = table_text::Get(id);
	if (!fmt)
	{
		std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), nothing shown\n", id);
		return;
	}
	char buf[256];
	std::snprintf(buf, sizeof buf, fmt, value);
	info_text_box->Display(buf, time, 0, false);
}

// What AddScore returns for a ScoreValue / SpecialAddScore call (FT 0x419f10: ScoreAdded + score * multiplier;
// SpecialAddScore forces multiplier 1). The shared ScoreValue / SpecialAddScore return nothing, so the amount is
// computed first.
int AddedByScoreValue(int score)
{
	return control::TableG->ScoreAdded + score * control::TableG->ScoreMultiplier;
}
int AddedBySpecialAddScore(int score)
{
	return control::TableG->ScoreAdded + score;
}

void OpenDragonJaw()   // 0x424130
{
	DragonState& s = S();
	s.JawOpen = 1;
	v_lite113->ActiveFlag = 1;
	v_lite114->ActiveFlag = 1;
	v_lite115->ActiveFlag = 1;
	v_lite119->Message(MessageCode::TLightFtTmpOverrideOn, 0.0f);
	dragon_mouth_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
	dragon_fire_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
	s.Mode9Fire = 1;
	v_lite113->MessageField = -1;
	a_jaw->Message(MessageCode::TBlockerMoveToFrame, 7.0f);
}

void ShowSlayTimeLeft()
{
	const int left = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
	if (left == 0)
		return;
	DisplayFormatted(3047, left / 1000, 0.5f);
}

// The 8 fire lights v_lite113, 114, 115, 58..62 (tag table at 0x43bb78).
TLight* FireLight(int i)
{
	switch (i)
	{
	case 0: return v_lite113;
	case 1: return v_lite114;
	case 2: return v_lite115;
	case 3: return v_lite58;
	case 4: return v_lite59;
	case 5: return v_lite60;
	case 6: return v_lite61;
	case 7: return v_lite62;
	default: Fatal("mode 9: fire light index out of range");
	}
}

void FireStep()
{
	DragonState& s = S();
	if (s.Mode9Fire < 1)
	{
		v_lite113->MessageField = v_lite113->MessageField - 1;
		if (v_lite113->MessageField == -1)
			s.Mode9Fire = 1;
		else
			FireLight(v_lite113->MessageField)->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	}
	else
	{
		v_lite113->MessageField = v_lite113->MessageField + 1;
		const int step = v_lite113->MessageField;
		if (step == 0)
			tabc58wave->Play(nullptr, "Mode9Controller");
		if (step >= 0 && step <= 7)
			FireLight(step)->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		else if (step == 8)
			s.Mode9Fire = -1;
	}
	if (v_lite113->MessageField > 5 && v_lite113->MessageField < 9)
		dragon_fire_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, 0.012f);
	else
		dragon_fire_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, 0.123f);
}

void MouthStep(TPinballComponent* caller)
{
	const int i = dragon_mouth_light_group->MessageField;
	dragon_mouth_light_group->MessageField = i + 1;
	if (i < 0)
		return;
	if (i < 9)
	{
		// Glyph strings at 0x439ac8: font slot 1 character codes 0x2c..0x33, two bytes each; entry 8 is empty.
		char glyph[2] = {static_cast<char>(i < 8 ? 0x2c + i : 0), 0};
		info_text_box->Display(glyph, -1.0f, 1, false);
		dragon_mouth_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, 0.4f);
		return;
	}
	if (i != 9)
		return;
	TPinballTable* table = control::TableG;
	table->LightGroup->Message(MessageCode::TLightFtResetOverride, 0.0f);
	const int added = AddedBySpecialAddScore(table->SuperJackpotScore);
	SpecialAddScore(caller, table->SuperJackpotScore);
	DisplayFormatted(3050, added, -1.0f);
	ScoreValue(caller, 1500000);
	AwardJackpot(0.0f, 0);
	v_lite33->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	v_lite34->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	v_lite35->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	v_lite36->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	table->ScoreMultiplier = 5;
	AwardArmorKickback();
	CenterPostControl(MessageCode::TBlockerEnable, a_center_post);
	ArmorPostControl(MessageCode::TBlockerEnable, a_bloce);
	v_lite28->Message(MessageCode::TLightTurnOnTimed, 30.0f);
	v_lite29->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	v_lite30->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	a_kout3->ActiveFlag = 1;
	v_lite31->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	BlockerFControl(MessageCode::TBlockerDisable, a_blocf);
	Mode9Controller(static_cast<int>(MessageCode::ControlModeEnd), nullptr);
	a_sink16->Message(MessageCode::TSinkResetTimer, -1.0f);
	dragon_letter_light_group->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	CheckMode15Start();
}

void ModeEnd()   // also reached from ControlBallDrained and SetTiltLock
{
	SetSwordState(3);
	v_lite71->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite72->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite73->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite74->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite75->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	QuestEndCleanup();
	CloseDragonJaw(1);
	v_lite77->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite78->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite79->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite76->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	CompleteQuest();
}

void BallDrained()
{
	info_text_box->MessageField = 2;
	mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
	ModeEnd();
}

void MissionComplete()
{
	DragonState& s = S();
	s.Mode9Intro = 1;
	light_sweeps->Message(MessageCode::TLightSweepsStart, 5.0f);
	a_sink18->ActiveFlag = 0;
	a_sink17->ActiveFlag = 0;
	s.Mode9Ramp = 0;
	s.Mode9Time = 30;
	if (v_lite37->light_on())
	{
		s.Mode9Time = 35;
		AwardArmorKickback();
		tabc24wave->Play(nullptr, "Mode9Controller");
	}
	if (v_lite38->light_on())
	{
		s.Mode9Time += 5;
		CenterPostControl(MessageCode::TBlockerEnable, a_center_post);
	}
	if (v_lite39->light_on())
	{
		s.Mode9Time += 5;
		ArmorPostControl(MessageCode::TBlockerEnable, a_bloce);
	}
	if (v_lite31->light_on())
		s.Mode9Time += 5;
	if (v_lite32->light_on())
		s.Mode9Time += 5;
	v_lite114->MessageField = 8;
	TLight* const hitLights[6] = {v_lite33, v_lite34, v_lite35, v_lite36, v_lite28, v_lite29};
	for (int i = 0; i < 6; ++i)
		if (hitLights[i]->light_on())
			v_lite114->MessageField = v_lite114->MessageField - 1;
	if (info_text_box->MessageField != 6 && info_text_box->MessageField != 0xd)
	{
		v_lite47->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_dvtraa->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrab->Message(MessageCode::TGateDisable, 0.0f);
	}
	a_dvtrba->Message(MessageCode::TGateDisable, 0.0f);
	a_dvtrbb->Message(MessageCode::TGateEnable, 0.0f);
	a_dvtrca->Message(MessageCode::TGateDisable, 0.0f);
	a_dvtrcb->Message(MessageCode::TGateEnable, 0.0f);
	if (info_text_box->MessageField != 6 && info_text_box->MessageField != 0xd)
	{
		v_lite125->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite126->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		a_dvtrda->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrdb->Message(MessageCode::TGateDisable, 0.0f);
	}
	ScoreValue(nullptr, 500000);
	const char* intro = table_text::Get(3048);
	if (intro)
		info_text_box->Display(intro, -1.0f, 0, false);
	else
		std::fprintf(stderr, "FT rules: text 3048 unavailable (EXE missing or string absent), nothing shown\n");
	v_lite29->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	v_lite30->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	a_kout3->ActiveFlag = 1;
	tabc57wave->Play(nullptr, "Mode9Controller");
}

void ModeResume()   // continues into ControlMissionStarted when no message is pending
{
	DragonState& s = S();
	tabc57wave->Play(nullptr, "Mode9Controller");
	mode_timer->Message(MessageCode::TTimerResetTimer, static_cast<float>(s.Mode9Time) / 2.0f);
	v_lite29->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	v_lite30->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	a_kout3->ActiveFlag = 1;
	OpenDragonJaw();
	if (info_text_box->HasPendingMessage())
		return;
	ShowSlayTimeLeft();
}

// ControlCollision; returns true when the one-way hit starts the mode timer and the ControlMissionStarted text follows.
bool Collision(TPinballComponent* caller)
{
	DragonState& s = S();
	switch (caller->TypeId)
	{
	case 1005: AddSuperJackpot(50000); return false;    // 0x3ed bumper
	case 1012: AddSuperJackpot(750000); return false;   // 0x3f4 kickout
	case 1016:                                          // 0x3f8 one-way
		if (s.Mode9Intro != 0 || caller != s_oneway2 || R().C.BallSave != 3)
			return false;
		mode_timer->Message(MessageCode::TTimerResetTimer, static_cast<float>(s.Mode9Time));
		return true;
	case 1017:                                          // 0x3f9 sink
		if (caller == a_sink19)
		{
			if (v_lite114->MessageField == 1)
			{
				control::TableG->LightGroup->Message(MessageCode::TLightFtTmpOverrideOn, 0.0f);
				CloseDragonJaw(0);
				mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
				dragon_mouth_light_group->MessageField = 0;
				dragon_mouth_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, 0.0001f);
				info_text_box->Clear();
			}
			s.Mode9Ramp = 0;
			return false;
		}
		if (caller == a_sink1 || caller == a_sink2)
			AddSuperJackpot(500000);
		return false;
	case 1019: AddSuperJackpot(100000); return false;   // 0x3fb solo target
	default: return false;
	}
}
}

void CloseDragonJaw(int n)   // 0x424050
{
	S().JawOpen = 0;
	MessageCode all;
	if (n == 0)
	{
		v_lite119->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
		dragon_mouth_light_group->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
		all = MessageCode::TLightFtTmpOverrideOff;
	}
	else
	{
		v_lite119->Message(MessageCode::TLightFtResetOverride, 0.0f);
		dragon_mouth_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
		all = MessageCode::TLightFtResetOverride;
	}
	dragon_fire_light_group->Message(all, 0.0f);
	dragon_mouth_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	dragon_fire_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	dragon_fire_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, -1.0f);
	v_lite113->ActiveFlag = 0;
	v_lite114->ActiveFlag = 0;
	v_lite115->ActiveFlag = 0;
	s_bwal170->ActiveFlag = 0;
	a_ramp107->ActiveFlag = 0;
	a_jaw->Message(MessageCode::TBlockerMoveToFrame, 0.0f);
}

void DragonMouthSinkControl(MessageCode code, TPinballComponent* caller)   // 0x427f10, a_sink19
{
	if (code != MessageCode::ControlCollision)
		return;
	if (info_text_box->MessageField != 9)
	{
		a_sink1->Message(MessageCode::TSinkResetTimer, 0.0f);
		return;
	}
	v_lite114->MessageField = v_lite114->MessageField - 1;
	if (v_lite114->MessageField == 0)
	{
		tabc56wave->Play(nullptr, "DragonMouthSinkControl");
		return;
	}
	const int left = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
	mode_timer->Message(MessageCode::TTimerResetTimer, ExtendedTimerSeconds(left));
	TSink* sink;
	switch (ft::RandIndex(6))
	{
	case 0: sink = a_sink4; break;
	case 1: sink = a_sink1; break;
	case 2: sink = a_sink2; break;
	case 3: sink = a_sink18; break;
	case 4: sink = a_sink17; break;
	default: sink = a_sink16; break;
	}
	sink->Message(MessageCode::TSinkResetTimer, 3.0f);
	bool award = true;   // false once a special award replaced the score text
	int score = 100000;
	switch (dragon_fire_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f))
	{
	case 0:
	case 1:
		break;
	case 2:
		score = 1000000;
		break;
	case 3:
		if (ft::RandIndex(5) == 0)
		{
			AwardJackpot(0.0f, 1);
			award = false;
		}
		break;
	case 4:
		if (ft::RandIndex(10) == 0)
		{
			tabc35wave->Play(nullptr, "DragonMouthSinkControl");
			const char* text = table_text::Get(3002);
			if (text)
				info_text_box->Display(text, 2.0f, 0, false);
			else
				std::fprintf(stderr, "FT rules: text 3002 unavailable (EXE missing or string absent), nothing shown\n");
			v_lite57->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			award = false;
		}
		break;
	default:
		if (ft::RandIndex(10) == 0)
		{
			table_add_extra_ball(0.0f);
			award = false;
		}
		break;
	}
	if (award)
	{
		tabc13wave->Play(nullptr, "DragonMouthSinkControl");
		const int added = AddedByScoreValue(score);
		ScoreValue(caller, score);
		DisplayFormatted(3008, added, 2.0f);
	}
	static const char bang[2] = {0x21, 0};   // glyph string at 0x439a58
	info_text_box->Display(bang, 3.5f, 1, false);
	DisplayFormatted(3049, v_lite114->MessageField, 5.0f);
}

void Mode9Controller(int code, TPinballComponent* caller)   // 0x42a150
{
	DragonState& s = S();
	bool started = false;   // run the ControlMissionStarted text
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlMissionComplete:
		MissionComplete();
		return;
	case MessageCode::ControlModeResume:
		ModeResume();
		return;
	case MessageCode::ControlMissionStarted:
		started = true;
		break;
	case MessageCode::ControlModeEnd:
		ModeEnd();
		return;
	case MessageCode::ControlBallDrained:
		BallDrained();
		return;
	case MessageCode::SetTiltLock:
		ResetSpells();
		BallDrained();
		return;
	case MessageCode::ControlRampPlaneLayer:
		if (a_ramp107 == caller)
			s.Mode9Ramp = 1;
		return;
	case MessageCode::ControlBallToLayer1:
		if (a_ramp107 == caller)
			s.Mode9Ramp = 0;
		return;
	case MessageCode::ControlNotifyTimerExpired:
		if (caller == dragon_fire_light_group)
			FireStep();
		else if (caller == dragon_mouth_light_group)
			MouthStep(caller);
		return;
	case MessageCode::ControlCollision:
		if (!caller)
			Fatal("mode 9: collision without a component");
		started = Collision(caller);
		break;
	case MessageCode::ControlTimerExpired:
	{
		if (!caller)
			Fatal("mode 9: timer expiry without a component");
		if (caller->TypeId == 1032)   // TTimer
		{
			if (caller != mode_timer)
				return;
			if (s.Mode9Ramp == 0)
			{
				mode_timer->MessageField = info_text_box->MessageField;
				CloseDragonJaw(0);
				info_text_box->MessageField = 3;
				MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
			}
			else
				mode_timer->Message(MessageCode::TTimerResetTimer, 0.05f);
			return;
		}
		if (caller->TypeId != 1038)   // TLightSweeps: the intro sweep ended
			return;
		s.Mode9Intro = 0;
		ModeStartLights();
		OpenDragonJaw();
		if (R().C.BallSave != 3)
		{
			mode_timer->Message(MessageCode::TTimerResetTimer, static_cast<float>(s.Mode9Time));
			MissionControl(static_cast<int>(MessageCode::ControlMissionStarted), nullptr);
		}
		return;
	}
	case MessageCode::ControlBlockerMoveDone:
		if (s.JawOpen != 0 && caller == a_jaw)
		{
			a_ramp107->ActiveFlag = 1;
			s_bwal170->ActiveFlag = 1;
			dragon_fire_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, 0.123f);
		}
		return;
	default:
		return;
	}
	if (started)
		ShowSlayTimeLeft();
}
}
