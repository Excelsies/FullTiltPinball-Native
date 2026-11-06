#include "pch.h"
#include "rules/ft_common.h"
#include "control.h"
#include "pb.h"
#include "table_text.h"
#include "timer.h"
#include "TBall.h"
#include "TCollisionComponent.h"
#include "TLight.h"
#include "TPinballTable.h"
#include "TTextBox.h"
#include <cstdint>
#include <cstdlib>

// Rules code shared by Dragon's Keep and Skulduggery. Port of the 14 functions that are identical between
// DRAGON.EXE and PIRATES.EXE; the per-table
// addresses are in ft_common.h. Failing fast: a missing link or box is a diagnostic and abort, never a silent skip.
namespace ft {
namespace {
[[noreturn]] void Fatal(const char* what)
{
	std::fprintf(stderr, "FT rules: fatal: %s\n", what);
	std::fflush(stderr);
	std::abort();
}

// make_component_link's two message boxes (the original shows a modal MessageBoxA, sets the link-failed flag and goes on;
// the table's make_links asserts on the flag at its end).
void LinkError(const char* problem, const char* tagName)
{
	std::fprintf(stderr, "FT rules: make_component_link: %s: %s\n", problem, tagName);
	pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, tagName, problem);
}
}

void FtRules::Handler(MessageCode code, TPinballComponent* caller)
{
	// The original calls MissionControl first, then the component's control function. Its ControlReloadScores branch
	// (put_scoring) re-applies the score table; here Control->Scores is read directly, so there is nothing to re-apply.
	MissionControl(static_cast<int>(code), caller);
	if (caller && caller->Control)
		caller->Control->ControlFunc(code, caller);
}

TPinballComponent* FtRules::MakeComponentLink(component_tag_base& tag)
{
	if (tag.GetComponent())
	{
		LinkError("tag is already linked", tag.Name);
		C.LinkFailed = 1;
		return tag.GetComponent();
	}
	if (!control::TableG)
		Fatal("make_component_link before control::TableG is set");
	for (TPinballComponent* comp : control::TableG->ComponentList)
	{
		if (comp->GroupName && !std::strcmp(comp->GroupName, tag.Name))
		{
			tag.SetComponent(comp);
			return comp;
		}
	}
	LinkError("component not found in the table", tag.Name);
	C.LinkFailed = 1;
	return nullptr;
}

void FtRules::NoteScorer(TPinballComponent* comp)
{
	// A null comp is valid (DRAGON 0x4230f0: only the last scorer is replaced); ScorePart checks it itself.
	if (C.LastScorer != comp)
	{
		if (C.LastScorer)
			Handler(MessageCode::ControlScorerReplaced, C.LastScorer);
		C.LastScorer = comp;
	}
}

int FtRules::ScorePart(TPinballComponent* comp, int index)
{
	if (!comp)
		Fatal("score part without a component");
	NoteScorer(comp);
	return control::TableG->AddScore(comp->get_scoring(static_cast<unsigned>(index)));
}

int FtRules::ScoreValue(TPinballComponent* comp, int score)
{
	NoteScorer(comp);
	return control::TableG->AddScore(score);
}

void FtRules::SpecialAddScore(TPinballComponent* comp, int score)
{
	TPinballTable* table = control::TableG;
	const bool bonusFlag = table->BonusScoreFlag;
	const bool jackpotFlag = table->JackpotScoreFlag;
	const int multiplier = table->ScoreMultiplier;
	NoteScorer(comp);
	table->BonusScoreFlag = false;
	table->JackpotScoreFlag = false;
	table->ScoreMultiplier = 1;
	table->AddScore(score);
	table->BonusScoreFlag = bonusFlag;
	table->JackpotScoreFlag = jackpotFlag;
	table->ScoreMultiplier = multiplier;
}

void FtRules::AddJackpot(int n)
{
	TPinballTable* table = control::TableG;
	table->JackpotScore += n;
	if (table->JackpotScore > 10000000)
		table->JackpotScore = 10000000;
}

void FtRules::UnselectModeController()
{
	SetMode(2);
	MissionControl(static_cast<int>(MessageCode::ControlMissionComplete), nullptr);
}

bool FtRules::CheckBallInControlBounds(TBall* ball, TPinballComponent* part)
{
	TCollisionComponent* box = dynamic_cast<TCollisionComponent*>(part);
	if (!ball || !box)
		Fatal("CheckBallInControlBounds needs a ball and a collision component");
	const float margin = control::TableG->CollisionCompOffset / 2.0f;
	return ball->ActiveFlag != 0
		&& box->AABB.XMin - margin <= ball->Position.X
		&& box->AABB.YMin - margin <= ball->Position.Y
		&& ball->Position.X <= box->AABB.XMax + margin
		&& ball->Position.Y <= box->AABB.YMax + margin;
}

void FtRules::TickerExpired(int, void* caller)
{
	Cur().TickerStart(caller != nullptr);
}

void FtRules::TickerStart(bool fromStart)
{
	if (C.TickerTimer)
		timer::kill(C.TickerTimer); // 0 when the entry has just fired
	C.TickerTimer = 0;
	if (!InfoBox)
		Fatal("ticker without the info text box");
	if (fromStart)
	{
		const char* text = table_text::GetData(C.QuoteFlag ? DataText::Quotes : DataText::Credits);
		if (!text)
		{
			static bool logged = false; // no substitute text; the reader has already logged the reason
			if (!logged)
				std::fprintf(stderr, "FT rules: ticker text unavailable (EXE missing or identity check failed)\n");
			logged = true;
			return;
		}
		C.TickerCursor = text;
	}
	if (!C.TickerCursor)
		Fatal("ticker continued before it was started");

	char line[200];
	int len = 0;
	while (*C.TickerCursor && *C.TickerCursor != '~')
	{
		if (len < static_cast<int>(sizeof line) - 1) // the original's 200-byte buffer; Overlay keeps 198 anyway
			line[len++] = *C.TickerCursor;
		++C.TickerCursor;
	}
	++C.TickerCursor;
	line[len] = '\0';
	InfoBox->Overlay(line, -1.0f, 0);
	const bool atEnd = C.TickerCursor[-1] == '\0';
	C.TickerTimer = timer::set(2.0f, atEnd ? reinterpret_cast<void*>(static_cast<std::intptr_t>(1)) : nullptr, TickerExpired);
}

void FtRules::TickerStop()
{
	if (C.TickerTimer)
		timer::kill(C.TickerTimer);
	C.TickerTimer = 0;
	if (!InfoBox)
		Fatal("ticker stop without the info text box");
	InfoBox->OverlayEnd();
}

void FtRules::Rebound(int which, MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	if (!ReboundLight[which])
		Fatal("rebounder light not linked");
	ReboundLight[which]->Message(MessageCode::TLightTurnOnTimed, 0.1f);
	ScorePart(caller, 0);
}

FtRules& Cur()
{
	FtRules* rules = dynamic_cast<FtRules*>(control::Rules);
	if (!rules)
		Fatal("control function called without Full Tilt rules");
	return *rules;
}

void BallSaveLightControl(MessageCode code, TPinballComponent*)
{
	FtRules& r = Cur();
	if (code != MessageCode::ControlTimerExpired)
		return;
	switch (r.C.BallSave)
	{
	case 1:
		if (!r.BallSaveLight)
			Fatal("ball save light not linked");
		r.C.BallSave = 0;
		r.BallSaveLight->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		break;
	case 2:
		if (!r.BallSaveLight)
			Fatal("ball save light not linked");
		r.C.BallSave = 1;
		r.BallSaveLight->Message(MessageCode::TLightFlasherStartTimed, 5.0f);
		break;
	case 3:
		if (!r.BallSaveLight)
			Fatal("ball save light not linked");
		r.C.BallSave = 2;
		r.BallSaveLight->Message(MessageCode::TLightTurnOnTimed, 10.0f);
		break;
	default:
		break;
	}
}

void LaunchOneWayControl(MessageCode code, TPinballComponent*)
{
	if (code == MessageCode::ControlCollision && Cur().C.BallSave == 3)
		BallSaveLightControl(MessageCode::ControlTimerExpired, nullptr);
}

void RebounderControl1(MessageCode code, TPinballComponent* caller) { Cur().Rebound(0, code, caller); }
void RebounderControl2(MessageCode code, TPinballComponent* caller) { Cur().Rebound(1, code, caller); }
void NullControl(MessageCode, TPinballComponent*) {}
}
