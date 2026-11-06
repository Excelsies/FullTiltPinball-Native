// Standalone check for FtRules::ScoreValue / SpecialAddScore with a null component.
// The original accepts a null scorer: it only replaces the last scorer (ControlScorerReplaced to the previous one) and
// adds the score. No game data. Needs the hook-build objects; build and run from the repo root with
//   re/test/native/ft_common_null_scorer_check.sh <out-dir>
// (the script compiles this file and rules/ft_common.cpp from source and links them with the other objects).
#include "pch.h"
#include "rules/ft_common.h"
#include "control.h"
#include "TPinballTable.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

struct TestRules : ft::FtRules
{
	std::vector<std::pair<int, TPinballComponent*>> Seen;   // MissionControl calls, in order
	void MakeLinks(TPinballTable*) override {}
	void ClearLinks() override {}
	void TableControlHandler(MessageCode) override {}
	void BackdoorKey(int, bool, bool) override {}
	void UnstuckBall(TBall&, int) override {}
	const char* GetText(int) override { return nullptr; }
	const char* const* CheatNames(int& c) override { c = 0; return nullptr; }
	void MissionControl(int code, TPinballComponent* caller) override { Seen.push_back(std::make_pair(code, caller)); }
	void SetMode(int) override {}
	int Mode() const override { return 0; }
	void Value(TPinballComponent* c, int s) { ScoreValue(c, s); }
	void Special(TPinballComponent* c, int s) { SpecialAddScore(c, s); }
	void SetLast(TPinballComponent* c) { C.LastScorer = c; }
	TPinballComponent* Last() const { return C.LastScorer; }
};

int main()
{
	// A zeroed table is enough for AddScore (no ctor: it would need the loaded table data).
	TPinballTable* table = static_cast<TPinballTable*>(std::calloc(1, sizeof(TPinballTable)));
	control::TableG = table;
	TestRules r;
	control::Rules = &r;
	table->ScoreMultiplier = 1;
	const int replaced = static_cast<int>(MessageCode::ControlScorerReplaced);
	TPinballComponent* prev = reinterpret_cast<TPinballComponent*>(0x1000);   // never dereferenced: its Control is not read (see below)

	// 1. null scorer, no previous scorer: score added, no message, LastScorer stays null.
	r.Value(nullptr, 100000);
	CHECK(table->CurScore == 100000);
	CHECK(r.Seen.empty());
	CHECK(r.Last() == nullptr);

	// 2. SpecialAddScore with a null scorer, no previous scorer.
	r.Special(nullptr, 5000);
	CHECK(table->CurScore == 105000);
	CHECK(r.Seen.empty());
	CHECK(r.Last() == nullptr);

	// 3. null scorer with a previous scorer: one ControlScorerReplaced to it, LastScorer becomes null.
	// (Handler also dereferences caller->Control, so use a real zeroed component, not a fake pointer.)
	TPinballComponent* comp = static_cast<TPinballComponent*>(std::calloc(1, sizeof(TPinballComponent)));
	(void)prev;
	r.SetLast(comp);
	r.Value(nullptr, 100);
	CHECK(table->CurScore == 105100);
	CHECK(r.Seen.size() == 1);
	if (r.Seen.size() == 1)
	{
		CHECK(r.Seen[0].first == replaced);
		CHECK(r.Seen[0].second == comp);
	}
	CHECK(r.Last() == nullptr);

	// 4. the same through SpecialAddScore.
	r.Seen.clear();
	r.SetLast(comp);
	r.Special(nullptr, 100);
	CHECK(table->CurScore == 105200);
	CHECK(r.Seen.size() == 1);
	if (r.Seen.size() == 1)
		CHECK(r.Seen[0].first == replaced && r.Seen[0].second == comp);
	CHECK(r.Last() == nullptr);

	std::printf(g_fail ? "FAIL (%d)\n" : "PASS\n", g_fail);
	return g_fail ? 1 : 0;
}
