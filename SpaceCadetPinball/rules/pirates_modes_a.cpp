#include "pch.h"
#include "rules/pirates_rules.h"
#include "midi.h"
#include "table_text.h"
#include "TBlocker.h"
#include "TKickout.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TLightSweeps.h"
#include "TRollover.h"
#include "TSink.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Skulduggery rules: the ship sink and the mode 4, 5 and 6 controllers.
// Ported from port_listing_pirates.c lines 3673-3836 and 5951-6039, 6507-6795 (FT 0x4283f0, 0x42ad70, 0x42c070, 0x42c430,
// PIRATES.EXE). Every message whose arguments the listing lost (ShipSinkControl, Mode4 mode_timer)
// and the Message codes of the Mode5 / ShipSink paths were read from the push sequences of the machine code, not taken
// from the listing: the listing's a_blocf / v_lite567 / fathom group sends in ShipSinkControl are Ghidra reading stale stack
// bytes, the real values are in the comments below.
using namespace pirates;

namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

int Code(MessageCode code) { return static_cast<int>(code); }

// FT type ids the controllers dispatch on (GetTypeId in the original).
const int TypePopupTarget = 1006; // TPopupTarget
const int TypeRollover = 1015;    // TRollover
const int TypeSink = 1017;        // TSink
const int TypeTimer = 1032;       // TTimer
const int TypeLightSweeps = 1038; // TLightSweeps

// crt_sprintf(buf, get_rc_string(id), value) + Display(box, buf, time, 0, 0). The string resources used here carry a
// single long decimal conversion; anything else is a fatal diagnostic (the original would print whatever the EXE holds).
void ShowLong(const char* format, long value, float time)
{
	if (!format)
		Fatal("mode text: string resource unavailable");
	const char* percent = std::strchr(format, '%');
	if (!percent || std::strncmp(percent, "%ld", 3) != 0 || std::strchr(percent + 3, '%') != nullptr)
		Fatal("mode text: string resource is not a single %ld conversion");
	char buffer[200];
	std::snprintf(buffer, sizeof buffer, format, value);
	info_text_box->Display(buffer, time, 0, false);
}

void ShowResourceText(int stringId, float time)
{
	const char* text = table_text::Get(stringId);
	if (!text)
		Fatal("mode text: string resource unavailable");
	info_text_box->Display(text, time, 0, false);
}

// The seconds-left line every mode shows on ControlMissionStarted: the timer's remaining time in milliseconds / 1000.
void ShowSecondsLeft(const char* format, int milliseconds) { ShowLong(format, static_cast<long>(milliseconds / 1000), 0.5f); }

// The common ModeEnd tail of modes 4-6: modes_played + 1, a_bloce frame 0, v_lite504 off, v_lite505 on.
void ModeEndTail()
{
	v_lite606->MessageField = v_lite606->MessageField + 1;
	a_bloce->Message(MessageCode::TBlockerSetFrame, 0.0f);
	v_lite504->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite505->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
}

// The TTimer / TLightSweeps branches of ControlTimerExpired shared by the three modes (identical machine code except the timer
// value and the work done at the sweep end): returns true when the sweep end was reached.
enum class Expired
{
	Nothing,
	Sweeps
};
Expired TimerBranch(TPinballComponent* caller)
{
	if (!caller)
		Fatal("mode controller: ControlTimerExpired without a caller");
	const int type = caller->TypeId;
	// A TTimer caller is handled by the controller itself (mode 6 clears a light first); any other type does nothing.
	return type == TypeLightSweeps ? Expired::Sweeps : Expired::Nothing;
}

// Mode 5 light pattern, FT 0x43d118: four light indices per step (rows of 16 bytes); -1 = no light in this column.
const int Mode5Rows[4][4] = {{0, 4, 8, 12}, {1, 5, 9, -1}, {2, 6, 10, -1}, {3, 7, 11, -1}};

// Mode 6 target hit glyphs, FT 0x43ab28 (9 bytes per entry): n hits = n glyph characters of the info box's second font (not text).
const char* const Mode6HitGlyph[9] = {"", "K", "KK", "KKK", "KKKK", "KKKKK", "KKKKKK", "KKKKKKK", "KKKKKKKK"};

// Mode 6 level by the number of lit lights of the eight wind / orbit / island groups: targets needed, points per target and the
// string id of the level text (3046-3049), FT 0x42c80d-0x42c864.
struct Mode6LevelSpec
{
	int Targets;
	int Points;
	int TextId;
};
Mode6LevelSpec Mode6LevelFor(int lit)
{
	if (lit <= 5)
		return {8, 10000, 3046};
	if (lit <= 10)
		return {7, 25000, 3047};
	if (lit <= 15)
		return {6, 50000, 3048};
	return {5, 100000, 3049};
}
}

namespace pirates {
// 0x4283f0: a_sink4. Only ControlCollision does anything; info_text_box->MessageField (the mode) selects the case.
void ShipSinkControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	State& s = S();
	switch (info_text_box->MessageField)
	{
	case 2: // idle mode: score text, or the extra ball when v_lite568 is lit
	default:
	{
		const bool idle = info_text_box->MessageField == 2;
		// push 0 / 0x39: a_blocf to frame 0; v_lite567 0xf (flash then stay on) 1.0; a_blocc, a_blocf, a_blocg to frame -1
		a_blocf->Message(MessageCode::TBlockerSetFrame, 0.0f);
		v_lite567->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		a_blocc->Message(MessageCode::TBlockerSetFrame, -1.0f);
		a_blocf->Message(MessageCode::TBlockerSetFrame, -1.0f);
		a_blocg->Message(MessageCode::TBlockerSetFrame, -1.0f);
		ReleaseHoldSink(-1.0f);
		if (idle && v_lite568->light_on())
		{
			v_lite568->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			AwardExtraBall(0.0f);
		}
		else
		{
			// ScorePart(caller, 0) once; its result is the %ld value of string 3003 (the listing's first ScorePart is a Ghidra duplicate)
			const int added = ScorePart(caller, 0);
			ShowLong(table_text::Get(3003), static_cast<long>(added), 2.0f);
		}
		tabb27wave->Play(nullptr, "ShipSinkControl");
		return;
	}
	case 4: // mode 4: a hit on the ship
	{
		DropLduggTargets();
		s.ModeCounter = s.ModeCounter + 1;
		const float shown = AwardJackpot(0.0f);
		fathom_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, shown); // push 0x2f / fstp: the award's display time
		ReleaseHoldSink(-1.0f);
		return;
	}
	case 9:
		tabb27wave->Play(nullptr, "ShipSinkControl");
		v_lite570->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		v_lite567->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		ReleaseHoldSink(-1.0f);
		return;
	case 11:
		v_mlite51->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		v_lite4->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite5->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite6->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite7->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite8->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite568->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite567->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		s.ModeCounter = 1;
		return;
	}
}

// 0x42ad70
void Mode4Controller(int code, TPinballComponent* caller)
{
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		if (TimerBranch(caller) == Expired::Sweeps)
		{
			if (S().NoHeldBall == 0)
				ReleaseHoldSink(0.0f);
			mode_timer->Message(MessageCode::TTimerResetTimer, 29.38f); // push 0x41eb0a3d
			MissionControl(Code(MessageCode::ControlMissionStarted), nullptr);
		}
		else if (caller->TypeId == TypeTimer && caller == mode_timer)
			MissionControl(Code(MessageCode::ControlModeEnd), nullptr);
		return;
	case MessageCode::ControlNotifyTimerExpired:
		if (caller == fathom_light_group)
		{
			ShowResourceText(3017, 2.0f);
			tabb26wave->Play(nullptr, "Mode4Controller");
		}
		return;
	case MessageCode::ControlMissionComplete:
		ModeStartCleanup();
		S().ModeCounter = 1;
		light_sweeps->Message(MessageCode::TLightSweepsStart, 0.0f);
		ResetLduggTargets();
		a_blocc->Message(MessageCode::TBlockerSetFrame, -1.0f);
		a_bloce->Message(MessageCode::TBlockerSetFrame, 1.0f);
		v_lite504->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		v_lite505->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_blocf->Message(MessageCode::TBlockerSetFrame, 0.0f);
		a_blocg->Message(MessageCode::TBlockerSetFrame, 0.0f);
		ScoreValue(nullptr, 200000);
		ShowResourceText(3060, -1.0f);
		v_lite4->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite5->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite6->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite7->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite8->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite567->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		midi::play_track(S().Music[1], true); // 0x43ab84
		tabb58wave->Play(nullptr, "Mode4Controller");
		return;
	case MessageCode::ControlMissionStarted:
	{
		const int left = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
		if (left != 0)
			ShowSecondsLeft(table_text::Get(3016), left);
		return;
	}
	case MessageCode::ControlModeEnd:
		v_lite567->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		ResetLduggTargets();
		ModeEndRestoreLights();
		ModeEndTail();
		return;
	case MessageCode::ControlBallDrained:
		mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f); // push 0xbf800000
		MissionControl(Code(MessageCode::ControlModeEnd), nullptr);
		return;
	default:
		return;
	}
}

// 0x42c070
void Mode5Controller(int code, TPinballComponent* caller)
{
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		if (TimerBranch(caller) == Expired::Sweeps)
		{
			if (S().NoHeldBall == 0)
				ReleaseHoldSink(0.0f);
			bermuda_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, 0.1f);
			a_kout1->Message(MessageCode::FtPartEnable, 0.0f);
			SetMode5WindLight(1);
			mode_timer->Message(MessageCode::TTimerResetTimer, 31.14f);
			MissionControl(Code(MessageCode::ControlMissionStarted), nullptr);
		}
		else if (caller->TypeId == TypeTimer && caller == mode_timer)
			MissionControl(Code(MessageCode::ControlModeEnd), nullptr);
		return;
	case MessageCode::ControlNotifyTimerExpired:
	{
		if (caller != bermuda_light_group)
			return;
		State& s = S();
		if (s.Mode5Step < 0 || s.Mode5Step > 3)
			Fatal("Mode5Controller: pattern step out of range");
		bermuda_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, 0.1f);
		for (int column = 0; column < 4; ++column)
			bermuda_light_group->Message(MessageCode::TLightGroupTurnOffAtIndex, static_cast<float>(Mode5Rows[s.Mode5Step][column]));
		s.Mode5Step = s.Mode5Step + 1;
		if (s.Mode5Step > 3)
			s.Mode5Step = 0;
		for (int column = 0; column < 4; ++column)
			bermuda_light_group->Message(MessageCode::TLightGroupTurnOnAtIndex, static_cast<float>(Mode5Rows[s.Mode5Step][column]));
		return;
	}
	case MessageCode::ControlMissionComplete:
		ModeStartCleanup();
		ScoreValue(nullptr, 200000);
		light_sweeps->Message(MessageCode::TLightSweepsStart, 8.0f);
		WindLightsOff();
		midi::play_track(S().Music[4], true); // 0x43ab90
		tabb66wave->Play(nullptr, "Mode5Controller");
		ShowResourceText(3063, -1.0f);
		return;
	case MessageCode::ControlMissionStarted:
	{
		const int left = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
		if (left != 0)
			ShowSecondsLeft(table_text::Get(3045), left);
		return;
	}
	case MessageCode::ControlModeEnd:
		WindLightsOff();
		bermuda_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, -1.0f);
		bermuda_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_kout1->Message(MessageCode::FtPartDisable, 0.0f);
		ModeEndRestoreLights();
		ModeEndTail();
		return;
	case MessageCode::ControlBallDrained:
		mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
		MissionControl(Code(MessageCode::ControlModeEnd), nullptr);
		return;
	default:
		return;
	}
}

// 0x42c430
void Mode6Controller(int code, TPinballComponent* caller)
{
	State& s = S();
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		if (TimerBranch(caller) == Expired::Sweeps)
		{
			if (s.NoHeldBall == 0)
				ReleaseHoldSink(0.0f);
			mode_timer->Message(MessageCode::TTimerResetTimer, 24.14f);
			MissionControl(Code(MessageCode::ControlMissionStarted), nullptr);
		}
		else if (caller->TypeId == TypeTimer && caller == mode_timer)
		{
			v_lite778->Message(MessageCode::TLightResetAndTurnOff, -1.0f);
			MissionControl(Code(MessageCode::ControlModeEnd), nullptr);
		}
		return;
	case MessageCode::ControlCollision:
	{
		if (!caller)
			Fatal("Mode6Controller: ControlCollision without a caller");
		const int type = caller->TypeId;
		if (type == TypePopupTarget)
		{
			s.ModeCounter = s.ModeCounter + 1;
			if (s.Mode6Targets == s.ModeCounter)
			{
				AwardJackpot(0.0f);
				s.ModeCounter = 0;
				return;
			}
			if (s.ModeCounter < 0 || s.ModeCounter > 8)
				Fatal("Mode6Controller: target counter outside the glyph table");
			tabb68wave->Play(nullptr, "Mode6Controller");
			ScoreValue(caller, s.Mode6Points);
			info_text_box->Display(Mode6HitGlyph[s.ModeCounter], 2.0f, 1, false);
			return;
		}
		if (type == TypeRollover)
		{
			if (caller == a_roll6)
			{
				BallSave() = 2; // 0x435d90: ball save re-armed
				v_lite569->Message(MessageCode::TLightTurnOnTimed, 10.0f);
			}
			return;
		}
		if (type != TypeSink)
			return;
		if (caller == a_sink17 || caller == a_sink15 || caller == a_sink14)
		{
			ShowResourceText(3050, 2.0f);
			Mode6Controller(Code(MessageCode::ControlBallDrained), caller); // push 0x53: the drain path
			LightMysteryAward(2.0f);
		}
		return;
	}
	case MessageCode::ControlMissionComplete:
	{
		ModeStartCleanup();
		ScoreValue(nullptr, 300000);
		light_sweeps->Message(MessageCode::TLightSweepsStart, 9.0f);
		midi::play_track(s.Music[5], true); // 0x43ab94
		tabb67wave->Play(nullptr, "Mode6Controller");
		a_blocc->Message(MessageCode::TBlockerSetFrame, 0.0f);
		a_blocf->Message(MessageCode::TBlockerSetFrame, -1.0f);
		a_blocg->Message(MessageCode::TBlockerSetFrame, 0.0f);
		ResetSkuTargets();
		ResetLduggTargets();
		ResetEryTargets();
		v_lite778->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		a_kout2->Message(MessageCode::FtPartEnable, 0.0f);
		v_lite1->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite2->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite3->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite4->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite5->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite6->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite7->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite8->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite9->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite10->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite11->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		int lit = 0;
		lit += left_island_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		lit += right_wind_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		lit += left_orbit_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		lit += right_orbit_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		lit += south_wind_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		lit += east_wind_1_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		lit += east_wind_2_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		lit += west_wind_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
		const Mode6LevelSpec level = Mode6LevelFor(lit);
		s.Mode6Targets = level.Targets;  // 0x435d40
		s.Mode6Points = level.Points;    // 0x435d2c
		s.Mode6Level = level.TextId;     // 0x435d50: the original copies the level string into a buffer; the id stands for it
		ShowResourceText(3067, -1.0f);
		return;
	}
	case MessageCode::ControlMissionStarted:
	{
		const int left = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
		if (left != 0)
		{
			// FT 0x42c8fc: the format is the .bss buffer 0x435d50, filled by ControlMissionComplete. Before any level was chosen it
			// holds the empty string, and crt_sprintf(buf, "", seconds) + Display(box, buf, 0.5, 0, 0) shows an empty line: no abort.
			if (s.Mode6Level == 0)
				info_text_box->Display("", 0.5f, 0, false);
			else
				ShowSecondsLeft(table_text::Get(s.Mode6Level), left);
		}
		return;
	}
	case MessageCode::ControlModeEnd:
		v_lite778->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		a_kout2->Message(MessageCode::FtPartDisable, 0.0f);
		ModeEndRestoreLights();
		ModeEndTail();
		return;
	case MessageCode::ControlBallDrained:
		mode_timer->Message(MessageCode::TTimerResetTimer, -1.0f);
		MissionControl(Code(MessageCode::ControlModeEnd), nullptr);
		return;
	default:
		return;
	}
}
}
