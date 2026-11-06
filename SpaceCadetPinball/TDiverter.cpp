#include "pch.h"
#include "TDiverter.h"

#include "control.h"
#include "loader.h"
#include "render.h"
#include "TBall.h"
#include "TLine.h"
#include "TPinballTable.h"
#include "timer.h"

TDiverter::TDiverter(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};

	EnableSoundId = 0;
	DisableSoundId = 0;
	std::fill(std::begin(PlayerActive), std::end(PlayerActive), static_cast<char>(0));

	loader::query_visual(groupIndex, 0, &visual);
	FlashTime = loader::query_float_attribute(groupIndex, 0, 407, 0.1f);

	if (visual.FloatArrCount == 2)
	{
		AddTwoSidedLine(visual.FloatArr, visual.CollisionGroup);
		return;
	}

	// Attribute 604: records {2.0, x0, y0, x1, y1, w}, ended by the first record whose marker is not 2.0.
	// The sixth float is skipped by the original too (unused).
	auto records = loader::query_float_attribute_opt(groupIndex, 0, 604);
	while (records && records[0] == 2.0f)
	{
		AddTwoSidedLine(records + 1, visual.CollisionGroup);
		records += 6;
	}
}

TDiverter::~TDiverter()
{
	// The original leaves its flash timer armed (latent use-after-free); the port kills it.
	timer::kill(TimerExpired);
}

void TDiverter::AddTwoSidedLine(const float* pts, unsigned int collisionGroup)
{
	// FT 0x41ba30: same two lines as TOneway, side tagged in WallValue (0 front, 1 back).
	vector2 p0{pts[0], pts[1]}, p1{pts[2], pts[3]};

	auto front = new TLine(this, &ActiveFlag, collisionGroup, p0, p1);
	front->Offset(PinballTable->CollisionCompOffset);
	front->place_in_grid(&AABB);
	EdgeList.push_back(front);
	front->WallValue = nullptr;

	auto back = new TLine(this, &ActiveFlag, collisionGroup, p1, p0);
	back->Offset(static_cast<float>(-PinballTable->CollisionCompOffset * 0.8));
	back->place_in_grid(&AABB);
	EdgeList.push_back(back);
	back->WallValue = reinterpret_cast<void*>(static_cast<size_t>(1));
}

int TDiverter::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TBlockerEnable:
	case MessageCode::TGateEnable:
	case MessageCode::FtPartEnable:
		ActiveFlag = 1;
		SpriteSet(0);
		loader::play_sound(EnableSoundId, this, "TDiverter1");
		break;
	case MessageCode::PlayerChanged:
		SwapPlayerState(value);
		PlayerActive[PinballTable->CurrentPlayer] = ActiveFlag;
		ActiveFlag = PlayerActive[static_cast<int>(std::floor(value))];
		Message(ActiveFlag ? MessageCode::TBlockerEnable : MessageCode::TBlockerDisable, 0.0f);
		break;
	case MessageCode::Reset:
		ResetPlayerState();
		std::fill(std::begin(PlayerActive), std::end(PlayerActive), static_cast<char>(0));
		// fall through to the disable path (FT 0x41be60 case 0x402)
	case MessageCode::TBlockerDisable:
	case MessageCode::TGateDisable:
	case MessageCode::FtPartDisable:
		ActiveFlag = 0;
		SpriteSet(-1);
		loader::play_sound(DisableSoundId, this, "TDiverter2");
		break;
	default: break;
	}
	return 0;
}

void TDiverter::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                          TEdgeSegment* edge)
{
	if (edge->WallValue == nullptr)
	{
		// Front side: bounce; no hard-hit branch (FT 0x41bc90).
		if (PinballTable->TiltLockFlag)
		{
			maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 1000000000.0, 0.0);
		}
		else if (maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, Threshold, Boost) >
			0.2f)
		{
			if (SoftHitSoundId)
				loader::play_sound(SoftHitSoundId, ball, "TDiverter3");
		}
		return;
	}

	// Back side: the ball passes.
	ball->not_again(edge);
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance -= distance;
	if (!PinballTable->TiltLockFlag)
	{
		if (HardHitSoundId)
			loader::play_sound(HardHitSoundId, ball, "TDiverter4");
		control::handler(MessageCode::ControlCollision, this);
	}
	if (ListBitmap && ListBitmap->size() > 1)
	{
		SpriteSet(1);
		timer::set(FlashTime, this, TimerExpired); // the id is discarded, as in the original
	}
}

void TDiverter::TimerExpired(int, void* caller)
{
	auto diverter = static_cast<TDiverter*>(caller);
	diverter->SpriteSet(diverter->ActiveFlag ? 0 : -1);
}

#ifdef FT_TEST_HOOK
bool TDiverter::HookGet(const std::string& field, double& out) const
{
	if (field == "flash_time")
	{
		out = FlashTime;
		return true;
	}
	if (field == "bitmap")
	{
		out = -1;
		if (ListBitmap && RenderSprite)
			for (size_t i = 0; i < ListBitmap->size(); ++i)
				if (ListBitmap->at(i).Bmp == RenderSprite->Bmp)
				{
					out = static_cast<double>(i);
					break;
				}
		return true;
	}
	return TCollisionComponent::HookGet(field, out);
}
#endif
