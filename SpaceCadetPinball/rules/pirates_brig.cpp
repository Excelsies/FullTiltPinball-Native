#include "pch.h"
#include "rules/pirates_rules.h"
#include "table_text.h"
#include "TKickback.h"
#include "TLight.h"
#include "TOneway.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TSound.h"
#include "TSoloTarget.h"
#include "TTextBox.h"
#include <cstdio>
#include <cstdlib>

// Skulduggery rules: the brig (ball lock), the jail target, mode 13, the lock release
// one-way and the hold sink. Ported from port_listing_pirates.c lines 631-643, 2255-2276, 3386-3443, 4362-4386, 5293-5319,
// 5335-5359 and 7151-7207 (FT 0x423630, 0x425d50, 0x4279b0, 0x429040, 0x42a340, 0x42a3d0, 0x42d590) and BallLockedInBrig
// (FT 0x424210, not in the export: ported from its 123 bytes of machine code, capstone, PIRATES.EXE).
namespace pirates {
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

enum { ModeIdle = 2, Mode7Num = 7, Mode12Num = 12, Mode13Num = 13, ModeTilt = 17 };

// The brig, the release one-way and the hold sink act in the modes below 12, in 14-16 and above 17 (never in 12, 13 or 17).
bool BrigActive(int mode)
{
	return mode < Mode12Num || (mode > Mode13Num && mode != ModeTilt);
}
}

// 0x423630 StartMode13
void StartMode13()
{
	ModeRunningLightOn();
	SetMode(Mode13Num);
	MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
}

// 0x425d50 SetJailLights: param 0 -> the three lights reset and off (0x14, 1.0); any other -> turned on, then a timed flasher that
// stays off (0x10, 1.0). The jail target state is cleared either way.
void SetJailLights(int param)
{
	const MessageCode code = static_cast<char>(param) == 0 ? MessageCode::TLightResetAndTurnOff : MessageCode::TLightFlasherStartTimedThenStayOff;
	if (static_cast<char>(param) != 0)
	{
		v_lite530->Message(MessageCode::TLightTurnOn, 0.0f);
		v_lite531->Message(MessageCode::TLightTurnOn, 0.0f);
		v_lite532->Message(MessageCode::TLightTurnOn, 0.0f);
	}
	v_lite530->Message(code, 1.0f);
	v_lite531->Message(code, 1.0f);
	v_lite532->Message(code, 1.0f);
	a_spot6->MessageField = 0;
}

// 0x4279b0 JailTargetControl (a_spot6): a_spot6->MessageField counts the hits 0, 1, 2. Mode 2: third hit starts mode 13.
// Mode 7: third hit lights the jail lights and gives the full-crew award. Other modes: nothing.
void JailTargetControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	const int mode = info_text_box->MessageField;
	if (mode == ModeIdle)
	{
		if (a_spot6->MessageField == 0)
		{
			tabb02wave->Play(nullptr, "JailTargetControl");
			a_spot6->MessageField = 1;
			ScorePart(caller, 0);
			v_lite530->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			return;
		}
		if (a_spot6->MessageField != 1)
		{
			a_spot6->MessageField = 0;
			ScorePart(caller, 0);
			StartMode13();
			return;
		}
		tabb02wave->Play(nullptr, "JailTargetControl");
		a_spot6->MessageField = 2;
		ScorePart(caller, 0);
		v_lite531->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		return;
	}
	if (mode != Mode7Num)
		return;
	if (a_spot6->MessageField == 0)
	{
		tabb02wave->Play(nullptr, "JailTargetControl");
		a_spot6->MessageField = 1;
		ScorePart(caller, 0);
		v_lite530->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		v_lite531->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		v_lite532->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		return;
	}
	if (a_spot6->MessageField != 1)
	{
		tabb61wave->Play(nullptr, "JailTargetControl");
		SetJailLights(1);
		AwardFullCrew(0.0f);
		ScorePart(caller, 0);
		return;
	}
	tabb02wave->Play(nullptr, "JailTargetControl");
	a_spot6->MessageField = 2;
	ScorePart(caller, 0);
	v_lite530->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	v_lite531->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	v_lite532->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x424210 BallLockedInBrig (123 bytes, __stdcall float; returns its argument, the chained text time). One ball less in play, one
// more locked; the second lock activates s_oneway12; the third starts mode 13 (no 0x52: a running mode is replaced); any
// other lock plays the lock sound and feeds the next ball (the plunger, 0x3fb, 0.96 s).
float BallLockedInBrig(float time)
{
	control::TableG->MultiballCount -= 1;
	S().LockedBalls += 1;
	if (S().LockedBalls == 2)
		s_oneway12->ActiveFlag = 1;
	if (control::TableG->MultiballCount < 0)
		control::TableG->MultiballCount = 0;
	if (S().LockedBalls == 3)
	{
		StartMode13();
		return time;
	}
	tabb39wave->Play(nullptr, "BallLockedInBrig");
	s_launcher1->Message(MessageCode::PlungerRelaunchBall, 0.96f);
	return time;
}

// 0x429040 BrigHoleControl (s_hole19): a ball the brig captures scores, shows string 3007 (2 s) and is locked.
void BrigHoleControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlBallCaptured || !BrigActive(info_text_box->MessageField))
		return;
	ScorePart(caller, 0);
	const char* text = table_text::Get(3007);
	if (!text)
		Fatal("BrigHoleControl: string 3007 unavailable");
	info_text_box->Display(text, 2.0f, 0, false);
	BallLockedInBrig(2.0f);
}

// 0x42a340 LockReleaseOneWayControl (s_oneway7): a ball passing the release one-way frees one locked ball.
void LockReleaseOneWayControl(MessageCode code, TPinballComponent*)
{
	if (code != MessageCode::ControlCollision || !BrigActive(info_text_box->MessageField))
		return;
	State& s = S();
	if (s.LockedBalls > 0)
	{
		s.LockedBalls -= 1;
		control::TableG->MultiballCount += 1;
	}
	if (s.LockedBalls == 0)
		a_kick1->Message(MessageCode::FtPartDisable, 0.0f);
	if (s.LockedBalls < 0)
		s.LockedBalls = 0;
}

// 0x42a3d0 HoldSinkControl (a_sink8): a captured ball restarts the sink's timer; when it expires outside mode 12 the
// release sound plays (the release itself is the sink's own timer behaviour).
void HoldSinkControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlTimerExpired)
	{
		if (info_text_box->MessageField != Mode12Num)
			tabb55wave->Play(nullptr, "HoldSinkControl");
		return;
	}
	if (code != MessageCode::ControlCollision)
		return;
	caller->Message(MessageCode::TSinkResetTimer, 0.0f);
}

// 0x42d590 Mode13Controller. Entry (MissionComplete) with no locked ball returns to mode 2 at once; else the
// locked balls join the balls in play. The drain of the last extra ball (MultiballCount 1) ends it.
void Mode13Controller(int code, TPinballComponent*)
{
	if (code == static_cast<int>(MessageCode::ControlMissionComplete))
	{
		if (S().LockedBalls == 0)
		{
			SetJailLights(1);
			SetMode(ModeIdle);
			MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
			return;
		}
		ModeStartCleanup();
		v_lite551->MessageField = 0xf; // the original writes the light's field here (0x43af6c), not the lava bank's a_spot11: reproduced as is
		RestoreLavaLights();
		control::TableG->MultiballCount += S().LockedBalls;
		S().LockedBalls = 0;
		v_lite530->Message(MessageCode::TLightTurnOn, 0.0f);
		v_lite531->Message(MessageCode::TLightTurnOn, 0.0f);
		v_lite532->Message(MessageCode::TLightTurnOn, 0.0f);
		v_lite530->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite531->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		v_lite532->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		a_kick1->Message(MessageCode::FtPartEnable, 0.0f);
		s_oneway12->ActiveFlag = 0;
		ScoreValue(nullptr, 100000);
		info_text_box->Clear();
		tabb95wave->Play(nullptr, "Mode13Controller");
	}
	else if (code != static_cast<int>(MessageCode::ControlMissionStarted))
	{
		if (code != static_cast<int>(MessageCode::ControlBallDrained))
			return;
		if (control::TableG->MultiballCount != 1)
			return;
		ModeEndRestoreLights();
		info_text_box->Clear();
		a_kick1->Message(MessageCode::FtPartDisable, 0.0f);
		SetMode(ModeIdle);
		MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
		SetJailLights(0);
		SetLavaLights(1);
		return;
	}
	const char* text = table_text::Get(3056);
	if (!text)
		Fatal("Mode13Controller: string 3056 unavailable");
	info_text_box->Display(text, -1.0f, 0, false);
}
}
