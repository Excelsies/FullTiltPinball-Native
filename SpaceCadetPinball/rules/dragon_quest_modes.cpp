#include "pch.h"
#include "rules/dragon_rules.h"
#include "TAccelerator.h"
#include "TBlocker.h"
#include "TDiverter.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TLightSweeps.h"
#include "TOneway.h"
#include "TPinballTable.h"
#include "TRamp.h"
#include "TSink.h"
#include "TSoloTarget.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include "table_text.h"
#include <cstdio>
#include <cstdlib>

// Dragon's Keep rules: the quest mode controllers Mode4Controller (0x428a80),
// Mode5Controller (0x4291c0), Mode6Controller (0x429510) and Mode8Controller
// (0x429ec0). Port of port_listing_dragon.c lines 4446-4711, 4712-4897 and 4995-5072. The shared tail of the four modes
// (ControlModeResume falling into ControlMissionStarted, the countdown text) is written once as ShowCountdown.
namespace dragon {
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// get_rc_string + TTextBox::Display(box, text, time, 0, 0). A string the EXE does not provide shows nothing (the reader
// has logged why), the rule state still advances.
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

// get_rc_string(id) as the format of crt_sprintf(buf[64], format, value), then Display(buf, time, 0, 0). The format is the
// game's own data.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
void ShowValueText(int id, int value, float time)
{
	const char* format = table_text::Get(id);
	if (!format)
	{
		std::fprintf(stderr, "FT rules: text %d unavailable (EXE missing or string absent), nothing shown\n", id);
		return;
	}
	char buffer[64];
	std::snprintf(buffer, sizeof buffer, format, static_cast<long>(value));
	info_text_box->Display(buffer, time, 0, false);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

void PlaySound(TSound* sound, const char* name)
{
	sound->Play(nullptr, name);
}

// ControlMissionStarted, also reached from ControlModeResume when no message is pending: the countdown text, shown only
// while the mode timer still has time left.
void ShowCountdown(int id)
{
	const int left = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
	if (left == 0)
		return;
	ShowValueText(id, left / 1000, 0.5f);
}

// The common ControlTimerExpired entry: returns 1 when the mode timer itself expired, 2 when the intro light sweep ended,
// 0 for any other timer or sweep.
int TimerKind(TPinballComponent* caller)
{
	if (!caller)
		Fatal("quest mode timer expiry without a component");
	if (caller->TypeId == 1032)   // TTimer
		return caller == mode_timer ? 1 : 0;
	return caller->TypeId == 1038 ? 2 : 0;   // TLightSweeps
}

// One gem collected in mode 4: light, score, text, and the gem bit for GemCollected.
void Gem(TLight* light, int score, int textId, unsigned bits, TPinballComponent* caller)
{
	light->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	const int added = R().ScoreValue(caller, score);
	ShowValueText(textId, added, 2.0f);
	GemCollected(bits);
}

bool IsOneOf(TPinballComponent* caller, TPinballComponent* a, TPinballComponent* b, TPinballComponent* c,
             TPinballComponent* d, TPinballComponent* e)
{
	return caller == a || caller == b || caller == c || caller == d || caller == e;
}

void Mode4Collision(TPinballComponent* caller)
{
	if (!caller)
		Fatal("mode 4: collision without a component");
	switch (caller->TypeId)
	{
	case 1005:   // 0x3ed bumper
		Gem(v_lite88, 10000, 3021, 1, caller);
		return;
	case 1016:   // 0x3f8 one-way
		if (s_oneway5 == caller)
		{
			v_lite95->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			AwardJackpot(0.0f, 1);
			GemCollected(0x40);
		}
		return;
	case 1017:   // 0x3f9 sink
		if (a_sink1 == caller || a_sink2 == caller)
			Gem(v_lite91, 100000, 3024, 0x20, caller);
		return;
	case 1019:   // 0x3fb solo target
		if (IsOneOf(caller, a_spot4, a_spot5, a_spot6, a_spot7, a_spot22))
			Gem(v_lite89, 50000, 3022, 2, caller);
		else if (a_spot3 == caller || a_spot2 == caller || a_spot1 == caller)
			Gem(v_lite90, 60000, 3023, 4, caller);
		else if (a_spot10 == caller || a_spot8 == caller || a_spot14 == caller || a_spot12 == caller)
			Gem(v_lite93, 70000, 3026, 0x10, caller);
		return;
	case 1036:   // 0x40c accelerator
		if (a_accl1 == caller)
			Gem(v_lite117, 100000, 3028, 0x100, caller);
		return;
	default:
		return;
	}
}

// The END part shared by ControlBallDrained (after its two writes) and ControlModeEnd.
void Mode4End()
{
	QuestEndCleanup();
	gem_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	gem_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
	CompleteQuest();
}

void Mode5End()
{
	QuestEndCleanup();
	v_lite63->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite64->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite66->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite66->Message(MessageCode::TLightFtResetOverride, 0.0f);
	v_lite65->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite65->Message(MessageCode::TLightFtResetOverride, 0.0f);
	v_lite91->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite91->Message(MessageCode::TLightFtResetOverride, 0.0f);
	CompleteQuest();
}

// The lights, gates and quest completion of the mode 6 end (ControlModeEnd, ControlBallDrained and the
// ControlCollision of a_sink18 with all four prison windows lit). `code` is the message that reached the function: the collision also ends
// the mode through MissionControl(ControlMissionComplete).
void Mode6End(int code)
{
	DragonState& s = S();
	if (s.Mode6TowerState == 0)
		v_lite48->Message(MessageCode::TLightResetAndTurnOff, -1.0f);
	else if (s.Mode6TowerState == 1)
		v_lite48->Message(MessageCode::TLightResetAndTurnOn, -1.0f);
	else
	{
		v_lite48->Message(MessageCode::TLightTurnOn, -1.0f);
		v_lite48->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	}
	QuestEndCleanup();
	prison_window_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	prison_window_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
	if (code == static_cast<int>(MessageCode::ControlCollision))
	{
		info_text_box->MessageField = 2;
		MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
	}
	CompleteQuest();
	if (info_text_box->MessageField != 6 && info_text_box->MessageField != 0xd)
	{
		v_lite47->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_dvtraa->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrab->Message(MessageCode::TGateDisable, 0.0f);
	}
	if (info_text_box->MessageField != 6 && info_text_box->MessageField != 0xd)
	{
		v_lite125->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite126->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		a_dvtrda->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrdb->Message(MessageCode::TGateDisable, 0.0f);
	}
}

// ControlNotifyTimerExpired of the prison window group: the lit-window chase. The original indexes List[i] with the
// group's message field and sends to the result; an index outside the list would dereference null there, so it is fatal here.
TLight* PrisonWindow(int index)
{
	TLightGroup* group = prison_window_light_group;
	if (index < 0 || static_cast<size_t>(index) >= group->List.size())
		Fatal("mode 6: window index outside the prison window group");
	return group->List[index];
}

void Mode6WindowChase()
{
	prison_window_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, 0.2f);
	int index = prison_window_light_group->MessageField;
	PrisonWindow(index)->Message(MessageCode::TLightFtResetOverride, 0.0f);
	index = index < 3 ? index + 1 : 0;
	prison_window_light_group->MessageField = index;
	TLight* next = PrisonWindow(index);
	if (!next->light_on())
		next->Message(MessageCode::TLightFtTmpOverrideOn, 0.0f);
	else
		next->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
}
}

void Mode4Controller(int code, TPinballComponent* caller)   // 0x428a80
{
	DragonState& s = S();
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		switch (TimerKind(caller))
		{
		case 2:
			ModeStartLights();
			ReleaseCapturedBall(a_sink1_tag, 0.0f);
			mode_timer->Message(MessageCode::TTimerResetTimer, 27.73f);
			MissionControl(static_cast<int>(MessageCode::ControlMissionStarted), nullptr);
			return;
		case 1:
			mode_timer->MessageField = info_text_box->MessageField;
			gem_light_group->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
			info_text_box->MessageField = 3;
			MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
			return;
		default:
			return;
		}
	case MessageCode::ControlCollision:
		Mode4Collision(caller);
		return;
	case MessageCode::ControlMissionComplete:
		light_sweeps->Message(MessageCode::TLightSweepsStart, 0.0f);
		gem_light_group->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		a_dvtrba->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrbb->Message(MessageCode::TGateDisable, 0.0f);
		a_dvtrca->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrcb->Message(MessageCode::TGateDisable, 0.0f);
		R().ScoreValue(nullptr, 100000);
		info_text_box->Clear();
		ShowText(3020, -1.0f);
		v_lite88->MessageField = 0;
		PlayTrack(s.TrackMode4);
		PlaySound(tabc46wave, "Mode4Controller");
		return;
	case MessageCode::ControlModeResume:
		PlaySound(tabc46wave, "Mode4Controller");
		gem_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
		mode_timer->Message(MessageCode::TTimerResetTimer, 15.0f);
		if (info_text_box->HasPendingMessage())
			return;
		ShowCountdown(3019);
		return;
	case MessageCode::ControlMissionStarted:
		ShowCountdown(3019);
		return;
	case MessageCode::ControlBallDrained:
		info_text_box->MessageField = 2;
		mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
		Mode4End();
		return;
	case MessageCode::ControlModeEnd:
		Mode4End();
		return;
	case MessageCode::ControlBallToLayer2:
		if (a_ramp103 == caller)
			Gem(v_lite94, 250000, 3027, 0x80, caller);
		else if (caller == a_ramp6)
			Gem(v_lite92, 200000, 3025, 8, caller);
		return;
	default:
		return;
	}
}

void Mode5Controller(int code, TPinballComponent* caller)   // 0x4291c0
{
	DragonState& s = S();
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		switch (TimerKind(caller))
		{
		case 2:
			ModeStartLights();
			ReleaseCapturedBall(a_sink1_tag, 0.0f);
			mode_timer->Message(MessageCode::TTimerResetTimer, 31.69f);
			MissionControl(static_cast<int>(MessageCode::ControlMissionStarted), nullptr);
			return;
		case 1:
			mode_timer->MessageField = info_text_box->MessageField;
			v_lite66->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
			v_lite65->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
			v_lite91->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
			info_text_box->MessageField = 3;
			MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
			return;
		default:
			return;
		}
	case MessageCode::ControlMissionComplete:
		light_sweeps->Message(MessageCode::TLightSweepsStart, 1.0f);
		v_lite91->MessageField = 0;
		a_sink2->MessageField = 0;
		R().ScoreValue(nullptr, 100000);
		info_text_box->Clear();
		ShowText(3031, -1.0f);
		v_lite63->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite64->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		PlaySound(tabc48wave, "Mode5Controller");
		PlayTrack(s.TrackB);
		return;
	case MessageCode::ControlModeResume:
		v_lite63->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite64->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		PlaySound(tabc48wave, "Mode5Controller");
		v_lite66->Message(MessageCode::TLightFtResetOverride, 0.0f);
		v_lite65->Message(MessageCode::TLightFtResetOverride, 0.0f);
		v_lite91->Message(MessageCode::TLightFtResetOverride, 0.0f);
		mode_timer->Message(MessageCode::TTimerResetTimer, 15.0f);
		if (info_text_box->HasPendingMessage())
			return;
		ShowCountdown(3030);
		return;
	case MessageCode::ControlMissionStarted:
		ShowCountdown(3030);
		return;
	case MessageCode::ControlBallDrained:
		info_text_box->MessageField = 2;
		mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
		Mode5End();
		return;
	case MessageCode::ControlModeEnd:
		Mode5End();
		return;
	default:
		return;
	}
}

void Mode6Controller(int code, TPinballComponent* caller)   // 0x429510
{
	DragonState& s = S();
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		switch (TimerKind(caller))
		{
		case 2:
			ModeStartLights();
			v_lite48->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
			v_lite48->Message(MessageCode::TLightFtResetOverride, 0.0f);
			watch_window_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			gate_window_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			prison_window_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			prison_window_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, 0.2f);
			ReleaseCapturedBall(a_sink18_tag, 0.0f);
			mode_timer->Message(MessageCode::TTimerResetTimer, 30.45f);
			MissionControl(static_cast<int>(MessageCode::ControlMissionStarted), nullptr);
			return;
		case 1:
			prison_window_light_group->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
			prison_window_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, -1.0f);
			mode_timer->MessageField = info_text_box->MessageField;
			info_text_box->MessageField = 3;
			MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
			return;
		default:
			return;
		}
	case MessageCode::ControlNotifyTimerExpired:
		if (prison_window_light_group == caller)
			Mode6WindowChase();
		return;
	case MessageCode::ControlCollision:
		if (a_sink18 != caller)
			return;
		if (prison_window_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f) != 4)
			return;
		mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
		prison_window_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, -1.0f);
		Mode6End(code);
		return;
	case MessageCode::ControlMissionComplete:
		if (!v_lite48->FlasherOnFlag)
			s.Mode6TowerState = (!v_lite48->LightOnFlag && !v_lite48->ToggledOnFlag) ? 0 : 1;
		else
			s.Mode6TowerState = 2;
		light_sweeps->Message(MessageCode::TLightSweepsStart, 2.0f);
		v_lite47->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		a_dvtraa->Message(MessageCode::TGateDisable, 0.0f);
		a_dvtrab->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrba->Message(MessageCode::TGateEnable, 0.0f);
		a_dvtrbb->Message(MessageCode::TGateDisable, 0.0f);
		a_dvtrca->Message(MessageCode::TGateDisable, 0.0f);
		a_dvtrcb->Message(MessageCode::TGateEnable, 0.0f);
		v_lite125->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite126->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_dvtrda->Message(MessageCode::TGateDisable, 0.0f);
		a_dvtrdb->Message(MessageCode::TGateEnable, 0.0f);
		R().ScoreValue(nullptr, 100000);
		info_text_box->Clear();
		ShowText(3033, -1.0f);
		PlaySound(tabc50wave, "Mode6Controller");
		PlayTrack(s.TrackMode6);
		v_lite48->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		return;
	case MessageCode::ControlMissionStarted:
		ShowCountdown(3032);
		return;
	case MessageCode::ControlModeResume:
		PlaySound(tabc50wave, "Mode6Controller");
		prison_window_light_group->Message(MessageCode::TLightFtResetOverride, 0.0f);
		mode_timer->Message(MessageCode::TTimerResetTimer, 15.0f);
		prison_window_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, 0.2f);
		if (info_text_box->HasPendingMessage())
			return;
		ShowCountdown(3032);
		return;
	case MessageCode::ControlModeEnd:
		Mode6End(code);
		return;
	case MessageCode::ControlBallDrained:
		info_text_box->MessageField = 2;
		mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
		Mode6End(code);
		return;
	default:
		return;
	}
}

void Mode8Controller(int code, TPinballComponent* caller)   // 0x429ec0
{
	DragonState& s = S();
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		switch (TimerKind(caller))
		{
		case 2:
			ModeStartLights();
			ReleaseCapturedBall(a_sink4_tag, 0.0f);
			mode_timer->Message(MessageCode::TTimerResetTimer, 30.69f);
			MissionControl(static_cast<int>(MessageCode::ControlMissionStarted), nullptr);
			return;
		case 1:
			well_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			spell_spot_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			mode_timer->MessageField = info_text_box->MessageField;
			well_light_group->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
			info_text_box->MessageField = 3;
			MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
			return;
		default:
			return;
		}
	case MessageCode::ControlMissionComplete:
		light_sweeps->Message(MessageCode::TLightSweepsStart, 4.0f);
		R().ScoreValue(nullptr, 100000);
		info_text_box->Clear();
		ShowText(3038, -1.0f);
		well_light_group->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		spell_spot_light_group->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		PlaySound(tabc55wave, "Mode8Controller");
		PlayTrack(s.TrackMode8);
		return;
	case MessageCode::ControlModeResume:
		PlaySound(tabc55wave, "Mode8Controller");
		well_light_group->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		spell_spot_light_group->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		mode_timer->Message(MessageCode::TTimerResetTimer, 15.0f);
		if (info_text_box->HasPendingMessage())
			return;
		ShowCountdown(3037);
		return;
	case MessageCode::ControlMissionStarted:
		ShowCountdown(3037);
		return;
	case MessageCode::ControlBallDrained:
		info_text_box->MessageField = 2;
		mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
		QuestEndCleanup();
		CompleteQuest();
		return;
	case MessageCode::ControlModeEnd:
		QuestEndCleanup();
		CompleteQuest();
		return;
	default:
		return;
	}
}
}
