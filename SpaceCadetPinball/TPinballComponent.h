#pragma once

struct SpriteData;
struct vector2i;
struct zmap_header_type;
struct gdrv_bitmap8;
struct render_sprite;
struct component_control;
struct vector2;
class TPinballTable;


enum class MessageCode
{
	// Private codes <1000, different meaning for each component
	TFlipperNull = 0,
	TFlipperExtend = 1,
	TFlipperRetract = 2,

	TLightTurnOff = 0,
	TLightTurnOn = 1,
	TLightGetLightOnFlag = 2,
	TLightGetFlasherOnFlag = 3,
	TLightFlasherStart = 4,
	TLightApplyMultDelay = 5,
	TLightApplyDelay = 6,
	TLightFlasherStartTimed = 7,
	TLightTurnOffTimed = 8,
	TLightTurnOnTimed = 9,
	TLightSetOnStateBmpIndex = 11,
	TLightIncOnStateBmpIndex = 12,
	TLightDecOnStateBmpIndex = 13,
	TLightResetTimed = 14,
	TLightFlasherStartTimedThenStayOn = 15,
	TLightFlasherStartTimedThenStayOff = 16,
	TLightToggleValue = 17,
	TLightResetAndToggleValue = 18,
	TLightResetAndTurnOn = 19,
	TLightResetAndTurnOff = 20,
	TLightToggle = 21,
	TLightResetAndToggle = 22,
	TLightSetMessageField = 23,
	TLightFtTmpOverrideOn = -24,
	TLightFtTmpOverrideOff = -25,
	TLightFtResetOverride = -26,

	TLightGroupNull = 0,
	TLightGroupStepBackward = 24,
	TLightGroupStepForward = 25,
	TLightGroupAnimationBackward = 26,
	TLightGroupAnimationForward = 27,
	TLightGroupLightShowAnimation = 28,
	TLightGroupGameOverAnimation = 29,
	TLightGroupRandomAnimationSaturation = 30,
	TLightGroupRandomAnimationDesaturation = 31,
	TLightGroupOffsetAnimationForward = 32,
	TLightGroupOffsetAnimationBackward = 33,
	TLightGroupReset = 34,
	TLightGroupTurnOnAtIndex = 35,
	TLightGroupTurnOffAtIndex = 36,
	TLightGroupGetOnCount = 37,
	TLightGroupGetLightCount = 38,
	TLightGroupGetMessage2 = 39,
	TLightGroupGetAnimationFlag = 40,
	TLightGroupResetAndTurnOn = 41,
	TLightGroupResetAndTurnOff = 42,
	TLightGroupRestartNotifyTimer = 43,
	TLightGroupFlashWhenOn = 44,
	TLightGroupToggleSplitIndex = 45,
	TLightGroupStartFlasher = 46,
	TLightGroupCountdownEnded = 47,

	TBumperSetBmpIndex = 11,
	TBumperIncBmpIndex = 12,
	TBumperDecBmpIndex = 13,

	TComponentGroupResetNotifyTimer = 48,

	TPopupTargetDisable = 49,
	TPopupTargetEnable = 50,

	TBlockerDisable = 51,
	TBlockerEnable = 52,
	TBlockerRestartTimeout = 59,

	TGateDisable = 53,
	TGateEnable = 54,

	TKickoutRestartTimer = 55,

	TSinkUnknown7 = 7,
	TSinkResetTimer = 56,

	TSoloTargetDisable = 49,
	TSoloTargetEnable = 50,

	TTimerResetTimer = 59,

	ControlBallCaptured = 57,
	ControlBallReleased = 58,
	ControlTimerExpired = 60,
	ControlNotifyTimerExpired = 61,
	ControlSpinnerLoopReset = 62,
	ControlCollision = 63,
	ControlEnableMultiplier = 64,
	ControlDisableMultiplier = 65,
	ControlMissionComplete = 66,
	ControlMissionStarted = 67,

	// Public codes 1000+, apply to all components
	LeftFlipperInputPressed = 1000,
	LeftFlipperInputReleased = 1001,
	RightFlipperInputPressed = 1002,
	RightFlipperInputReleased = 1003,
	PlungerInputPressed = 1004,
	PlungerInputReleased = 1005,
	Pause = 1008,
	Resume = 1009,
	LooseFocus = 1010,
	SetTiltLock = 1011,
	ClearTiltLock = 1012,
	StartGamePlayer1 = 1013,
	NewGame = 1014,
	PlungerFeedBall = 1015,
	PlungerStartFeedTimer = 1016,
	PlungerLaunchBall = 1017,
	PlungerRelaunchBall = 1018,
	PlayerChanged = 1020,
	SwitchToNextPlayer = 1021,
	GameOver = 1022,
	Reset = 1024,

	// Full Tilt only codes (value = Full Tilt code + 0x100; msgmap.json new_members). Sent by the FT table rules and classes only.
	TBlockerSetFrame = 0x139,
	TBlockerMoveToFrame = 0x13a,
	TBlockerEnableNow = 0x13b,
	TBlockerDisableNow = 0x13c,
	FtPartEnable = 0x14b,
	FtPartDisable = 0x14c,
	THoleEjectBall = 0x143,
	TKickoutDestroyBall = 0x14e,
	TLightSweepsStart = 0x11b,
	TTimerGetTimeLeft = 0x148,
	ControlBlockerMoveDone = 0x13d,
	ControlScorerReplaced = 0x14d,
	ControlModeResume = 0x151,
	ControlModeEnd = 0x152,
	ControlBallDrained = 0x153,
	ControlRampPlaneLayer = 0x154,
	ControlBallToLayer1 = 0x155,
	ControlBallToLayer2 = 0x156,
	ControlReloadScores = 0x4fd,
	TableBuilt = 0x4f7,
};

class TPinballComponent
{
public:
	TPinballComponent(TPinballTable* table, int groupIndex, bool loadVisuals);
	virtual ~TPinballComponent();
	virtual int Message(MessageCode code, float value);
	virtual void port_draw();
#ifdef FT_TEST_HOOK
	// Test-hook field access (read/write of class-specific members by name); false = unknown field.
	virtual bool HookGet(const std::string& field, double& out) const;
	virtual bool HookSet(const std::string& field, double value);
#endif
	// FT 0x413550: MessageField and all four per-player copies = 0.
	void ResetPlayerState();
	// FT 0x413570: save MessageField for the current player, load the copy of player floor(value).
	void SwapPlayerState(float value);
	int get_scoring(unsigned int index) const;
	virtual vector2 get_coordinates();
	void SpriteSet(int index) const;
	void SpriteSetBall(int index, vector2i pos, float depth) const;

	char UnusedBaseFlag;
	char ActiveFlag;
	int MessageField;
	// Per-player copy of MessageField (Full Tilt tables only; FT +0x1a..+0x26, helpers 0x413550/0x413570).
	int PlayerMessageField[4];
	// DAT part type this component was built from (set by the part switch; 0 until then).
	int TypeId;
	char* GroupName;
	component_control* Control;
	int GroupIndex;
	render_sprite* RenderSprite;
	TPinballTable* PinballTable;
	std::vector<SpriteData>* ListBitmap;
private:
	float VisualPosNormX;
	float VisualPosNormY;
};
