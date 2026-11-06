#include "pch.h"
#include "TLightSweeps.h"

#include "control.h"
#include "loader.h"
#include "timer.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TTableLayer.h"

TLightSweeps::TLightSweeps(TPinballTable* table, TLightGroup* lights, int groupIndex)
	: TPinballComponent(table, groupIndex, false)
{
	// FT 0x40c210: the grid is laid over the table layer's bounds, which the part switch must have built.
	assertm(table->TableLayer != nullptr, "TLightSweeps: table layer missing");
	const auto layer = table->TableLayer;
	XMin = layer->XMin;
	YMin = layer->YMin;
	CellWidth = (layer->XMax - layer->XMin) / static_cast<float>(Columns);
	CellHeight = (layer->YMax - layer->YMin) / static_cast<float>(Rows);
	InvCellWidth = static_cast<float>(1.0 / CellWidth);
	InvCellHeight = static_cast<float>(1.0 / CellHeight);
	SweepGroupIndex = groupIndex;
	TLightSweeps::Reset();

	// Only lights built before this part are binned; the master list is walked from the back.
	if (lights)
	{
		for (auto it = lights->List.rbegin(); it != lights->List.rend(); ++it)
		{
			auto light = *it;
			// Lights without attribute 501 are skipped silently (FT 0x40d8d0 returns NULL).
			auto pos = loader::query_float_attribute_opt(light->GroupIndex, 0, 501);
			if (!pos)
				continue;
			Cells[Row(pos[1]) * Columns + Column(pos[0])].push_back(light);
		}
	}
}

TLightSweeps::~TLightSweeps()
{
	// The original does not kill the step timer here; a pending one would call into a deleted object.
	Reset();
}

// clamp(floor(v), 0, hi) as the inlined macro of FT 0x40c364-0x40c529 (x87 extended precision there, doubles here).
static int ClampFloor(double v, int hi)
{
	double t = std::floor(v);
	if (!(t < hi))
		t = hi;
	if (t <= 0.0)
		t = 0.0;
	return static_cast<int>(t);
}

int TLightSweeps::Column(float x) const
{
	return ClampFloor((static_cast<double>(x) - XMin) * InvCellWidth, Columns - 1);
}

int TLightSweeps::Row(float y) const
{
	return ClampFloor((static_cast<double>(y) - YMin) * InvCellHeight, Rows - 1);
}

int TLightSweeps::Message(MessageCode code, float value)
{
	// FT 0x40c660. No ActiveFlag test and no MessageField = code; always returns 0.
	switch (code)
	{
	case MessageCode::TLightSweepsStart:
		if (SweepGroupIndex)
			StartSweep(static_cast<int>(value));
		break;
	case MessageCode::SetTiltLock:
		Reset();
		break;
	case MessageCode::PlayerChanged:
		Reset();
		SwapPlayerState(value);
		break;
	case MessageCode::Reset:
		ResetPlayerState();
		Reset();
		break;
	default:
		break;
	}
	return 0;
}

void TLightSweeps::Reset()
{
	if (Timer)
	{
		timer::kill(Timer);
		Timer = 0;
	}
}

void TLightSweeps::StartSweep(int state)
{
	// A second start while a sweep runs does not kill the running timer (FT quirk, reproduced):
	// Timer keeps only the newest id.
	auto stepTime = loader::query_float_attribute_opt(SweepGroupIndex, state, 1602);
	StepTime = stepTime ? *stepTime : 0.1f;
	auto onTime = loader::query_float_attribute_opt(SweepGroupIndex, state, 1604);
	LightOnTime = onTime ? *onTime : StepTime;
	auto centre = loader::query_float_attribute_opt(SweepGroupIndex, state, 1600);
	if (centre)
	{
		CenterCol = Column(centre[0]);
		CenterRow = Row(centre[1]);
	}
	else
	{
		CenterCol = 0;
		CenterRow = 0;
	}
	auto expanding = loader::query_float_attribute_opt(SweepGroupIndex, state, 1601);
	Expanding = expanding ? static_cast<int>(*expanding) : 0;
	auto repeats = loader::query_float_attribute_opt(SweepGroupIndex, state, 1603);
	RepeatsLeft = repeats ? static_cast<int>(*repeats) : 1;

	// Largest squared distance from the centre cell to the four corner cells.
	const int w = Columns - CenterCol - 1;
	const int h = Rows - CenterRow - 1;
	int dist2 = std::max(std::max(CenterCol * CenterCol + CenterRow * CenterRow, w * w + h * h),
	                     std::max(w * w + CenterRow * CenterRow, CenterCol * CenterCol + h * h));
	MaxRadius = static_cast<int>(std::sqrt(static_cast<double>(static_cast<float>(dist2))));
	Radius = Expanding ? 0 : MaxRadius;
	Step();
}

void TLightSweeps::Step()
{
	// FT 0x40c5b0: the first ring drawn is MaxRadius-1 (contracting) or 1 (expanding).
	bool passDone;
	if (!Expanding)
	{
		--Radius;
		passDone = Radius < 0;
	}
	else
	{
		++Radius;
		passDone = Radius > MaxRadius;
	}
	if (passDone)
	{
		--RepeatsLeft;
		if (RepeatsLeft > 0)
		{
			Radius = Expanding ? 0 : MaxRadius;
		}
		else
		{
			control::handler(MessageCode::ControlTimerExpired, this);
			return; // sweep over, no new timer
		}
	}
	LightRing(CenterCol, CenterRow, Radius);
	Timer = timer::set(StepTime, this, TimerExpired);
}

void TLightSweeps::TimerExpired(int, void* caller)
{
	auto sweeps = static_cast<TLightSweeps*>(caller);
	sweeps->Timer = 0;
	sweeps->Step();
}

void TLightSweeps::LightRing(int cx, int cy, int r)
{
	// FT 0x40c110: midpoint circle, 8 symmetric cells per step in the original call order.
	int i = 0;
	int err = r * r;
	int limit = (r - 1) * r;
	do
	{
		LightCell(cx + i, cy - r);
		LightCell(cx - i, cy - r);
		LightCell(cx + i, cy + r);
		LightCell(cx - i, cy + r);
		LightCell(cx + r, cy - i);
		LightCell(cx - r, cy - i);
		const int row = cy + i;
		++i;
		LightCell(cx + r, row);
		LightCell(cx - r, row);
		err += i * -2 + 1;
		if (err <= limit)
		{
			const int twice = r * 2;
			--r;
			limit -= twice - 2;
		}
	}
	while (i <= r);
}

void TLightSweeps::LightCell(int col, int row)
{
	// FT 0x40c0a0: out-of-grid cells are ignored; the cell's lights are messaged from the last to the first.
	if (col < 0 || col >= Columns || row < 0 || row >= Rows)
		return;
	const auto& cell = Cells[row * Columns + col];
	for (auto it = cell.rbegin(); it != cell.rend(); ++it)
		(*it)->Message(MessageCode::TLightFtTmpOverrideOn, LightOnTime);
}

#ifdef FT_TEST_HOOK
bool TLightSweeps::HookGet(const std::string& field, double& out) const
{
	if (field == "columns") out = Columns;
	else if (field == "rows") out = Rows;
	else if (field == "xmin") out = XMin;
	else if (field == "ymin") out = YMin;
	else if (field == "inv_cell_w") out = InvCellWidth;
	else if (field == "inv_cell_h") out = InvCellHeight;
	else if (field == "radius") out = Radius;
	else if (field == "repeats_left") out = RepeatsLeft;
	else if (field == "timer") out = Timer != 0 ? 1 : 0;
	else return false;
	return true;
}
#endif
