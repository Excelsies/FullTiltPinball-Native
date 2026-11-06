#pragma once
#include "TPinballComponent.h"

class TLight;
class TLightGroup;

// Full Tilt part type 1038 ("light_sweeps", FT ctor 0x40c210): a light show controller without
// visuals. Bins every playfield light into a 15 x 25 grid over the table layer's bounds; message
// TLightSweepsStart (FT 0x1b, value = sweep state) plays rings of cells around a centre, one ring per
// timer step, and each lit cell flashes its lights with TLightFtTmpOverrideOn. Reports
// ControlTimerExpired to control::handler when the sweep is over.
class TLightSweeps : public TPinballComponent
{
public:
	static constexpr int Columns = 15;
	static constexpr int Rows = 25;

	TLightSweeps(TPinballTable* table, TLightGroup* lights, int groupIndex);
	~TLightSweeps() override;
	int Message(MessageCode code, float value) override;
#ifdef FT_TEST_HOOK
	bool HookGet(const std::string& field, double& out) const override;
#endif
	// FT 0x40c590: stop stepping (lights already flashed go back by their own override timers).
	virtual void Reset();

	static void TimerExpired(int timerId, void* caller);

private:
	void StartSweep(int state);
	void Step();
	void LightRing(int cx, int cy, int r);
	void LightCell(int col, int row);
	int Column(float x) const;
	int Row(float y) const;

	std::vector<TLight*> Cells[Columns * Rows]; // index row * Columns + col, lights in append order
	int SweepGroupIndex{};
	float CellWidth{};
	float CellHeight{};
	float InvCellWidth{};
	float InvCellHeight{};
	float XMin{};
	float YMin{};
	float StepTime{};
	float LightOnTime{};
	int CenterCol{};
	int CenterRow{};
	int Expanding{};
	int RepeatsLeft{};
	int Radius{};
	int MaxRadius{};
	int Timer{};
};
