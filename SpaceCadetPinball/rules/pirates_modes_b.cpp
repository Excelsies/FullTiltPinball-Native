#include "pch.h"
#include "rules/pirates_rules.h"
#include "midi.h"
#include "table_text.h"
#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TLightSweeps.h"
#include "TMagnet.h"
#include "TSoloTarget.h"
#include "TSound.h"
#include "TTextBox.h"
#include "TTimer.h"
#include <cstdio>
#include <cstdlib>

// Skulduggery rules: the magnets, the mode 7, 9 and 10 controllers and
// the mode 10 bank checks. Ported from port_listing_pirates.c lines 1508-1655, 1847-1918, 6139-6254 and 6796-6936
// (FT 0x424c70, 0x424d30, 0x424fb0, 0x425440, 0x425490, 0x425550, 0x42b420, 0x42ca00, 0x42cc10). Where the listing and the
// machine code disagree the machine code was followed: CheckLavaBankMode10 returns its argument (0x425536
// `fld [esp+4]`), the listing's `1.0` is Ghidra reading the last pushed flasher value.
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
const int TypeTimer = 1032;       // TTimer
const int TypeLightSweeps = 1038; // TLightSweeps
const int TypeLight = 1002;       // TLight

// The magnet announcement is one glyph character of the info box's second font (0x43aafc: 'E', NUL); not game text.
const char MagnetGlyph[] = "E";

// get_rc_string(id) + crt_sprintf(buf, fmt, seconds) + Display(box, buf, 0.5, 0, 0). The three Mode controllers use string
// resources whose single conversion is a long decimal; anything else is a fatal diagnostic (the original would
// have printed whatever the EXE holds).
void ShowSecondsLeft(int stringId, int milliseconds)
{
	const char* format = table_text::Get(stringId);
	if (!format)
		Fatal("mode text: string resource unavailable");
	const char* percent = std::strchr(format, '%');
	if (!percent || std::strncmp(percent, "%ld", 3) != 0 || std::strchr(percent + 3, '%') != nullptr)
		Fatal("mode text: string resource is not a single %ld conversion");
	char buffer[200];
	std::snprintf(buffer, sizeof buffer, format, static_cast<long>(milliseconds / 1000));
	info_text_box->Display(buffer, 0.5f, 0, false);
}

void ShowResourceText(int stringId)
{
	const char* text = table_text::Get(stringId);
	if (!text)
		Fatal("mode text: string resource unavailable");
	info_text_box->Display(text, -1.0f, 0, false);
}

// Wait for the lights, then the common ModeEnd tail of the three modes (7, 9, 10): modes_played + 1,
// a_bloce frame 0, v_lite504 off, v_lite505 on.
void ModeEndTail()
{
	v_lite606->MessageField = v_lite606->MessageField + 1;
	a_bloce->Message(MessageCode::TBlockerSetFrame, 0.0f);
	v_lite504->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	v_lite505->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
}

// The TTimer and TLightSweeps branches shared by modes 9 and 10 (identical machine code except the timer value).
// Returns after the work; the caller returns too.
void TimerBranchMode9Like(TPinballComponent* caller, float modeSeconds)
{
	if (!caller)
		Fatal("mode controller: ControlTimerExpired without a caller");
	const int type = caller->TypeId;
	if (type == TypeTimer)
	{
		if (mode_timer == caller)
			MissionControl(Code(MessageCode::ControlModeEnd), nullptr);
		return;
	}
	if (type != TypeLightSweeps)
		return;
	if (S().NoHeldBall == 0)
		ReleaseHoldSink(0.0f);
	mode_timer->Message(MessageCode::TTimerResetTimer, modeSeconds);
	MissionControl(Code(MessageCode::ControlMissionStarted), nullptr);
}
}

namespace pirates {
// 0x424c70
void MagnetsOff()
{
	s_magnet1->Message(MessageCode::FtPartDisable, 0.0f);
	v_lite560->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	s_magnet2->Message(MessageCode::FtPartDisable, 0.0f);
	v_lite561->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	s_magnet3->Message(MessageCode::FtPartDisable, 0.0f);
	v_lite562->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	s_magnet4->Message(MessageCode::FtPartDisable, 0.0f);
	v_lite563->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	s_magnet5->Message(MessageCode::FtPartDisable, 0.0f);
	v_lite564->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	s_magnet6->Message(MessageCode::FtPartDisable, 0.0f);
	v_lite565->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x424d30: `time` is the text display time the callers chain (the listing drops it); the result is time + 2.0
float ActivateRandomMagnet(float time)
{
	TLight* const lights[6] = {v_lite560, v_lite561, v_lite562, v_lite563, v_lite564, v_lite565};
	TMagnet* const magnets[6] = {s_magnet1, s_magnet2, s_magnet3, s_magnet4, s_magnet5, s_magnet6};
	int unlit[6];
	int count = 0;
	for (int i = 0; i < 6; ++i)
		if (!lights[i]->light_on())
			unlit[count++] = i;
	if (count == 0) // every magnet light is lit: all six are candidates
	{
		for (int i = 0; i < 6; ++i)
			unlit[i] = i;
		count = 6;
	}
	const int pick = unlit[ft::RandIndex(count)];
	magnets[pick]->Message(MessageCode::FtPartEnable, 0.0f);
	lights[pick]->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	keg_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0f); // the result is not used
	time = time + 2.0f;
	info_text_box->Display(MagnetGlyph, time, 1, false);
	return time;
}

// 0x424fb0: every award gets display time 0
void RandomMode7Award()
{
	switch (ft::RandIndex(4))
	{
	case 0: AwardMapPiece(0.0f); break;
	case 1: AwardNextSpecialTreasure(0.0f); break;
	case 2: AwardCoins(0.0f); break;
	default: AwardExtraBall(0.0f); break;
	}
}

// 0x425440: the award follows the Treasure X / wind index 0x435d04 (+1 afterwards)
float Mode10Award(float time)
{
	State& s = S();
	float result;
	switch (s.TreasureXIndex)
	{
	case 0: result = AwardNextSpecialTreasure(time); break;
	case 1: result = AwardCoins(time); break;
	case 2: result = AwardMapPiece(time); break;
	default: result = AwardExtraBall(time); break;
	}
	s.TreasureXIndex = s.TreasureXIndex + 1;
	return result;
}

// 0x425490
float CheckLavaBankMode10(float time)
{
	if ((v_lite550->MessageField & 0xf) == 0xf)
	{
		v_lite550->MessageField = 0;
		v_lite551->Message(MessageCode::TLightResetTimed, 0.0f);
		v_lite550->Message(MessageCode::TLightResetTimed, 0.0f);
		v_lite549->Message(MessageCode::TLightResetTimed, 0.0f);
		v_lite548->Message(MessageCode::TLightResetTimed, 0.0f);
		v_lite551->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		v_lite550->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		v_lite549->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		v_lite548->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		return time;
	}
	ScoreValue(nullptr, 30000);
	return time;
}

// 0x425550: bits 0..3 = the lava bank, bits 4..6 = the Ery targets of the mode 10 counter
float CheckMode10Banks(float time)
{
	State& s = S();
	if ((s.ModeCounter & 0x70) == 0x70)
	{
		s.ModeCounter = s.ModeCounter & 0xf;
		return Mode10Award(time);
	}
	if ((s.ModeCounter & 0xf) == 0xf)
	{
		s.ModeCounter = s.ModeCounter & 0x70;
		time = Mode10Award(time);
	}
	return time;
}

// 0x42b420
void Mode7Controller(int code, TPinballComponent* caller)
{
	// The sweep-end branch re-enters the ControlTimerExpired case once, with v_lite547 as the caller (a `goto` in the
	// original), so two passes at most.
	for (int pass = 0; pass < 2; ++pass)
	{
		switch (static_cast<MessageCode>(code))
		{
		case MessageCode::ControlTimerExpired:
		{
			if (!caller)
				Fatal("Mode7Controller: ControlTimerExpired without a caller");
			const int type = caller->TypeId;
			if (type == TypeLight)
			{
				if (caller != v_lite547 && caller != v_lite546 && caller != v_lite545)
					return;
				TLight* flashing;
				switch (ft::RandIndex(3))
				{
				case 0: flashing = v_lite547; break;
				case 1: flashing = v_lite546; break;
				default: flashing = v_lite545; break;
				}
				// (float)(RandIndex(150) + 100) / 1000.0: x87 fild + fdiv m64, stored as a float (0x42b4e0 ff.)
				const int tenths = ft::RandIndex(150) + 100;
				flashing->Message(MessageCode::TLightFlasherStartTimed, static_cast<float>(static_cast<double>(tenths) / 1000.0));
				return;
			}
			if (type == TypeTimer)
			{
				if (caller != mode_timer)
					return;
				v_lite547->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
				v_lite546->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
				v_lite545->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
				MissionControl(Code(MessageCode::ControlModeEnd), nullptr);
				return;
			}
			if (type != TypeLightSweeps)
				return;
			if (S().NoHeldBall == 0)
				ReleaseHoldSink(0.0f);
			mode_timer->Message(MessageCode::TTimerResetTimer, 27.4f);
			MissionControl(Code(MessageCode::ControlMissionStarted), nullptr);
			code = Code(MessageCode::ControlTimerExpired);
			caller = v_lite547;
			continue;
		}
		case MessageCode::ControlMissionComplete:
			ModeStartCleanup();
			SetJailLights(0);
			ScoreValue(nullptr, 200000);
			light_sweeps->Message(MessageCode::TLightSweepsStart, 7.0f);
			midi::play_track(S().Music[3], true); // 0x43ab8c
			tabb65wave->Play(nullptr, "Mode7Controller");
			ResetSkuTargets();
			v_lite547->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			v_lite546->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			v_lite545->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			a_spot6->MessageField = 0;
			v_lite530->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			v_lite531->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			v_lite532->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			ShowResourceText(3062);
			v_lite1->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
			v_lite2->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
			v_lite3->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
			return;
		case MessageCode::ControlMissionStarted:
		{
			const int left = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
			if (left != 0)
				ShowSecondsLeft(3025, left);
			return;
		}
		case MessageCode::ControlModeEnd:
			SetJailLights(0);
			ResetSkuTargets();
			MagnetsOff();
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

// 0x42ca00
void Mode9Controller(int code, TPinballComponent* caller)
{
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		TimerBranchMode9Like(caller, 48.28f);
		return;
	case MessageCode::ControlMissionComplete:
		ModeStartCleanup();
		ScoreValue(nullptr, 100000);
		light_sweeps->Message(MessageCode::TLightSweepsStart, 10.0f);
		midi::play_track(S().Music[5], true); // 0x43ab94
		tabb73wave->Play(nullptr, "Mode9Controller");
		a_blocc->Message(MessageCode::TBlockerSetFrame, 0.0f);
		a_blocf->Message(MessageCode::TBlockerSetFrame, 0.0f);
		a_blocg->Message(MessageCode::TBlockerSetFrame, 0.0f);
		ShowResourceText(3064);
		return;
	case MessageCode::ControlMissionStarted:
	{
		const int left = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
		if (left != 0)
			ShowSecondsLeft(3051, left);
		return;
	}
	case MessageCode::ControlModeEnd:
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

// 0x42cc10
void Mode10Controller(int code, TPinballComponent* caller)
{
	switch (static_cast<MessageCode>(code))
	{
	case MessageCode::ControlTimerExpired:
		TimerBranchMode9Like(caller, 29.38f);
		return;
	case MessageCode::ControlMissionComplete:
		ModeStartCleanup();
		ScoreValue(nullptr, 250000);
		light_sweeps->Message(MessageCode::TLightSweepsStart, 11.0f);
		v_lite9->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite10->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite11->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite551->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite550->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite549->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		v_lite548->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		ResetEryTargets();
		midi::play_track(S().Music[1], true); // 0x43ab84
		tabb75wave->Play(nullptr, "Mode10Controller");
		ShowResourceText(3065);
		return;
	case MessageCode::ControlMissionStarted:
	{
		const int left = mode_timer->Message(MessageCode::TTimerGetTimeLeft, 0.0f);
		if (left != 0)
			ShowSecondsLeft(3052, left);
		return;
	}
	case MessageCode::ControlModeEnd:
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
