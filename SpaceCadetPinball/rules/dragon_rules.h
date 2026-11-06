#pragma once
#include "rules/ft_common.h"
#include "control.h"

// Dragon's Keep rules: the class, every rule global of the table (DragonState, FT addresses in the
// comments), the tag references and the declarations of every function the dragon_*.cpp files share. C++11 only,
// like the rest of the engine.

// The rule globals of DRAGON.EXE. Plain members, not per player (the original leaks them across players; the port does
// the same). Rule state kept in components (mode = info_text_box MessageField, ...) is read through the components.
struct DragonState
{
	int TargetsHit = 0;      // 0x434d08 targets hit this ball (bonus line 3012)
	int Spells = 0;          // 0x434d24 spells awarded this ball (bonus line 3015)
	int DrainState = 0;      // 0x43bb6c 0 none, 1 drained, 2 next ball, 3 saved ball
	int LastBall = 0;        // 0x439b1c last-ball tower release in progress (1, 2 if tilted)
	int BallDrained = 0;     // 0x439b14 ball drained (no new quest selection until next ball)
	int BallHeld = 0;        // 0x439b10 ball held by the rules at mode change
	int BonusTotal = 0;      // 0x434d00
	int BonusSkip = 0;       // 0x434d0c
	int BonusStep = 0;       // 0x434d10 0..8
	int EggOffer = 0;        // 0x434d1c egg shop offer level
	int EggSpell = 0;        // 0x434d28 egg shop: spell picked for the offer
	int Mode6TowerState = 0; // 0x434d14 mode 6: tower state at start
	int Mode9Time = 0;       // 0x434cf8 mode 9 time
	int Mode9Intro = 0;      // 0x434d04 mode 9 intro light sweep running
	int Mode9Ramp = 0;       // 0x434d18 mode 9 ball on a_ramp107's upper plane
	int Mode9Fire = 0;       // 0x439b0c mode 9 fire direction / hits
	int JawOpen = 0;         // 0x439b18 dragon jaw open
	int Vestige = 0;         // 0x439b04 vestige of table_unlimited_balls (written, never read)
	int CheatState = 0;      // 0x43bb70 cheat sequence state (last key)
	int CheatMode = 0;       // 0x43bb74 cheat family (1 = 1MAX, 2 = NMAX, 3..5 = FMAX / PMAX / QMAX)
	// music slots (0x439ae0..0x439afc): handles of midi::load_track, 0 = never loaded; filled by TableBuilt with the
	// default track (TrackNewGame) standing in for a missing one, as the original does. 0x438140 (cheat PB) is TBall::AltBallIndex.
	int TrackNewGame = 0;    // 0x439af8 (also mode 2 and the NewGame track)
	int TrackA = 0;          // 0x439ae0 (list entry 2)
	int TrackMode4 = 0;      // 0x439ae4 (mode 4)
	int TrackB = 0;          // 0x439ae8 (modes 5, 14; the original copies slot ae0)
	int TrackMode6 = 0;     // 0x439aec (mode 6)
	int TrackMode7 = 0;       // 0x439af0 (mode 7)
	int TrackMode8 = 0;     // 0x439af4 (mode 8)
	int TrackGameOver = 0;   // 0x439afc (mode 0)
};

class DragonRules final : public ft::FtRules
{
public:
	DragonState St;
	// The shared state and helpers of ft::FtRules, public here so that the free functions of the other files can reach them.
	using ft::FtRules::C;
	using ft::FtRules::BallSaveLight;
	using ft::FtRules::InfoBox;
	using ft::FtRules::ScorePart;
	using ft::FtRules::ScoreValue;
	using ft::FtRules::SpecialAddScore;
	using ft::FtRules::AddJackpot;
	using ft::FtRules::UnselectModeController;
	using ft::FtRules::CheckBallInControlBounds;

	void MakeLinks(TPinballTable* table) override;
	void ClearLinks() override;
	void TableControlHandler(MessageCode code) override;
	void BackdoorKey(int key, bool isLeftFlip, bool isRightFlip) override;
	void UnstuckBall(TBall& ball, int dt) override;
	void TextEvent(int ftCode) override;
	const char* GetText(int id) override;
	const char* const* CheatNames(int& count) override;
	void DumpState(std::vector<std::pair<const char*, int>>& out) override;
	void MissionControl(int code, TPinballComponent* caller) override;   // 0x42c020
	void SetMode(int m) override;
	int Mode() const override;
};

class TAccelerator;
class TBlocker;
class TBumper;
class TDiverter;
class TDrain;
class TFlipper;
class THole;
class TKickback;
class TKickout;
class TLight;
class TLightGroup;
class TLightRollover;
class TLightSweeps;
class TOneway;
class TPlunger;
class TRamp;
class TRampExit;
class TRollover;
class TSink;
class TSoloTarget;
class TSound;
class TTextBox;
class TTimer;
class TTripwire;
class TWall;
namespace dragon {
// control functions of the table (the shared ones are ft::* in ft_common.h)
void AcceleratorControl(MessageCode, TPinballComponent*);
void ArmorPostControl(MessageCode, TPinballComponent*);
void BallDrainControl(MessageCode, TPinballComponent*);
void BlockerFControl(MessageCode, TPinballComponent*);
void BumperControl(MessageCode, TPinballComponent*);
void CenterPostControl(MessageCode, TPinballComponent*);
void DragonEggRampExitControl1(MessageCode, TPinballComponent*);
void DragonEggRampExitControl2(MessageCode, TPinballComponent*);
void DragonMouthSinkControl(MessageCode, TPinballComponent*);
void LizardSinkControl1(MessageCode, TPinballComponent*);
void LizardSinkControl2(MessageCode, TPinballComponent*);
void RidgeControl(MessageCode, TPinballComponent*);
void GateSinkControl1(MessageCode, TPinballComponent*);
void GateSinkControl2(MessageCode, TPinballComponent*);
void GateWindowTripwireControl(MessageCode, TPinballComponent*);
void HoleControl(MessageCode, TPinballComponent*);
void JackpotRampControl(MessageCode, TPinballComponent*);
void Kickout1Control(MessageCode, TPinballComponent*);
void Kickout2Control(MessageCode, TPinballComponent*);
void Kickout3Control(MessageCode, TPinballComponent*);
void Kickout3LightControl(MessageCode, TPinballComponent*);
void LightTargetControl1(MessageCode, TPinballComponent*);
void LightTargetControl2(MessageCode, TPinballComponent*);
void OutlaneKickbackControl(MessageCode, TPinballComponent*);
void OutlaneKickerLightControl(MessageCode, TPinballComponent*);
void OutlaneKickerRolloverControl(MessageCode, TPinballComponent*);
void OutlaneRolloverControl1(MessageCode, TPinballComponent*);
void OutlaneRolloverControl2(MessageCode, TPinballComponent*);
void PassRockAControl(MessageCode, TPinballComponent*);
void PassRockBControl(MessageCode, TPinballComponent*);
void PassTargetControl1(MessageCode, TPinballComponent*);
void PassTargetControl2(MessageCode, TPinballComponent*);
void PassTargetControl3(MessageCode, TPinballComponent*);
void PassTargetControl4(MessageCode, TPinballComponent*);
void PlungerControl(MessageCode, TPinballComponent*);
void PrisonSinkControl1(MessageCode, TPinballComponent*);
void PrisonSinkControl2(MessageCode, TPinballComponent*);
void PrisonWindowTripwireControl(MessageCode, TPinballComponent*);
void QuestSelectTargetControl(MessageCode, TPinballComponent*);
void Ramp6Control(MessageCode, TPinballComponent*);
void RightFlipperControl(MessageCode, TPinballComponent*);
void RolloverScoreControl1(MessageCode, TPinballComponent*);
void RolloverScoreControl2(MessageCode, TPinballComponent*);
void SpellTargetControl1(MessageCode, TPinballComponent*);
void SpellTargetControl2(MessageCode, TPinballComponent*);
void SpellTargetControl3(MessageCode, TPinballComponent*);
void SpellTargetControl4(MessageCode, TPinballComponent*);
void SpellTargetControl5(MessageCode, TPinballComponent*);
void SuperJackpotOneWayControl(MessageCode, TPinballComponent*);
void SwordTargetControl1(MessageCode, TPinballComponent*);
void SwordTargetControl2(MessageCode, TPinballComponent*);
void SwordTargetControl3(MessageCode, TPinballComponent*);
void TowerLockTargetControl1(MessageCode, TPinballComponent*);
void TowerLockTargetControl2(MessageCode, TPinballComponent*);
void TowerLockTargetControl3(MessageCode, TPinballComponent*);
void WatchSinkControl1(MessageCode, TPinballComponent*);
void WatchSinkControl2(MessageCode, TPinballComponent*);
void WatchWindowTripwireControl(MessageCode, TPinballComponent*);
void WellSinkControl(MessageCode, TPinballComponent*);
void WindowRolloverControl(MessageCode, TPinballComponent*);
extern component_info score_components[75];
extern component_tag_base* simple_components[150];
// tags
extern component_tag<TKickback> a_kick1_tag;
extern component_tag<TLightGroup> outlane_kicker_light_group_tag;
extern component_tag<TSound> tabc24wave_tag;
extern component_tag<TDiverter> a_dvtraa_tag;
extern component_tag<TDiverter> a_dvtrab_tag;
extern component_tag<TDiverter> a_dvtrba_tag;
extern component_tag<TDiverter> a_dvtrbb_tag;
extern component_tag<TDiverter> a_dvtrca_tag;
extern component_tag<TDiverter> a_dvtrcb_tag;
extern component_tag<TDiverter> a_dvtrda_tag;
extern component_tag<TDiverter> a_dvtrdb_tag;
extern component_tag<TBlocker> a_bloce_tag;
extern component_tag<TBlocker> a_blocf_tag;
extern component_tag<TLight> v_lite47_tag;
extern component_tag<TBumper> a_bump1_tag;
extern component_tag<TBumper> a_bump2_tag;
extern component_tag<TBumper> a_bump3_tag;
extern component_tag<TBumper> a_bump4_tag;
extern component_tag<TWall> a_rebo1_tag;
extern component_tag<TWall> a_rebo2_tag;
extern component_tag<TLight> v_lite120_tag;
extern component_tag<TLight> v_lite121_tag;
extern component_tag<TTextBox> info_text_box_tag;
extern component_tag<TDrain> drain_tag;
extern component_tag<TPlunger> s_plunger1_tag;
extern component_tag<TLight> v_lite40_tag;
extern component_tag<TOneway> s_oneway2_tag;
extern component_tag<TSoloTarget> a_spot4_tag;
extern component_tag<TSoloTarget> a_spot5_tag;
extern component_tag<TSoloTarget> a_spot6_tag;
extern component_tag<TSoloTarget> a_spot7_tag;
extern component_tag<TSoloTarget> a_spot22_tag;
extern component_tag<TLight> v_lite3_tag;
extern component_tag<TLight> v_lite4_tag;
extern component_tag<TLight> v_lite5_tag;
extern component_tag<TLight> v_lite6_tag;
extern component_tag<TLight> v_lite7_tag;
extern component_tag<TLightGroup> spell_spot_light_group_tag;
extern component_tag<TSound> tabc35wave_tag;
extern component_tag<TSoloTarget> a_spot3_tag;
extern component_tag<TSoloTarget> a_spot2_tag;
extern component_tag<TSoloTarget> a_spot1_tag;
extern component_tag<TLight> v_lite8_tag;
extern component_tag<TLight> v_lite9_tag;
extern component_tag<TLight> v_lite10_tag;
extern component_tag<TLight> v_lite22_tag;
extern component_tag<TLight> v_lite23_tag;
extern component_tag<TLight> v_lite24_tag;
extern component_tag<TLight> v_lite25_tag;
extern component_tag<TLight> v_lite26_tag;
extern component_tag<TLight> v_lite27_tag;
extern component_tag<TLightGroup> dragon_letter_light_group_tag;
extern component_tag<TSoloTarget> a_spot10_tag;
extern component_tag<TSoloTarget> a_spot8_tag;
extern component_tag<TSoloTarget> a_spot14_tag;
extern component_tag<TSoloTarget> a_spot12_tag;
extern component_tag<TLight> v_lite14_tag;
extern component_tag<TLight> v_lite13_tag;
extern component_tag<TLight> v_lite12_tag;
extern component_tag<TLight> v_lite11_tag;
extern component_tag<TLightGroup> gargoyle_spot_light_group_tag;
extern component_tag<TBlocker> a_rocka_tag;
extern component_tag<TBlocker> a_rockb_tag;
extern component_tag<TSoloTarget> a_spot27_tag;
extern component_tag<TLight> v_lite21_tag;
extern component_tag<TSound> tabc96wave_tag;
extern component_tag<TLight> v_lite57_tag;
extern component_tag<TSound> tabc21wave_tag;
extern component_tag<TSoloTarget> a_spot19_tag;
extern component_tag<TSoloTarget> a_spot26_tag;
extern component_tag<TSoloTarget> a_spot28_tag;
extern component_tag<TLight> v_lite16_tag;
extern component_tag<TLight> v_lite17_tag;
extern component_tag<TLight> v_lite18_tag;
extern component_tag<TBlocker> a_sword_tag;
extern component_tag<TLight> v_lite58_tag;
extern component_tag<TLight> v_lite59_tag;
extern component_tag<TLight> v_lite60_tag;
extern component_tag<TLight> v_lite61_tag;
extern component_tag<TLight> v_lite62_tag;
extern component_tag<TLightGroup> dragon_fire_light_group_tag;
extern component_tag<TSink> a_sink19_tag;
extern component_tag<TBlocker> a_jaw_tag;
extern component_tag<TLight> v_lite113_tag;
extern component_tag<TLight> v_lite114_tag;
extern component_tag<TLight> v_lite115_tag;
extern component_tag<TLightGroup> dragon_mouth_light_group_tag;
extern component_tag<TLight> v_lite119_tag;
extern component_tag<TRamp> a_ramp107_tag;
extern component_tag<TWall> s_bwal170_tag;
extern component_tag<TSound> tabc13wave_tag;
extern component_tag<TSoloTarget> a_spot30_tag;
extern component_tag<TSoloTarget> a_spot31_tag;
extern component_tag<TLight> v_lite20_tag;
extern component_tag<TLight> v_lite19_tag;
extern component_tag<TBlocker> a_center_post_tag;
extern component_tag<TSink> a_sink1_tag;
extern component_tag<TSink> a_sink2_tag;
extern component_tag<TLight> v_lite63_tag;
extern component_tag<TLight> v_lite64_tag;
extern component_tag<TLight> v_lite66_tag;
extern component_tag<TLight> v_lite65_tag;
extern component_tag<TSound> tabc16wave_tag;
extern component_tag<TSink> a_sink4_tag;
extern component_tag<TLightGroup> well_light_group_tag;
extern component_tag<TRamp> a_ramp103_tag;
extern component_tag<TLight> v_lite67_tag;
extern component_tag<TSound> tabc18wave_tag;
extern component_tag<TRamp> a_ramp6_tag;
extern component_tag<TLight> v_lite70_tag;
extern component_tag<TOneway> s_oneway5_tag;
extern component_tag<TLight> v_lite68_tag;
extern component_tag<TAccelerator> a_accl1_tag;
extern component_tag<TTripwire> s_trip1_tag;
extern component_tag<TTripwire> s_trip2_tag;
extern component_tag<TLight> v_lite69_tag;
extern component_tag<TRollover> a_roll7_tag;
extern component_tag<TRollover> a_roll5_tag;
extern component_tag<TRollover> a_roll2_tag;
extern component_tag<TRollover> a_roll6_tag;
extern component_tag<TLight> v_lite56_tag;
extern component_tag<TRollover> a_roll1_tag;
extern component_tag<TLight> v_lite55_tag;
extern component_tag<THole> s_hole118_tag;
extern component_tag<TLightGroup> dragon_egg_light_group_tag;
extern component_tag<TSound> tabc27wave_tag;
extern component_tag<TSound> tabc32wave_tag;
extern component_tag<TSound> tabc33wave_tag;
extern component_tag<TSound> tabc34wave_tag;
extern component_tag<TRampExit> s_rexit19_tag;
extern component_tag<TRampExit> s_rexit18_tag;
extern component_tag<TFlipper> a_flip1_tag;
extern component_tag<TFlipper> a_flip3_tag;
extern component_tag<TFlipper> a_flip2_tag;
extern component_tag<TKickout> a_kout1_tag;
extern component_tag<TLight> v_lite37_tag;
extern component_tag<TLight> v_lite38_tag;
extern component_tag<TLight> v_lite39_tag;
extern component_tag<TLight> v_lite45_tag;
extern component_tag<TLight> v_lite33_tag;
extern component_tag<TLight> v_lite34_tag;
extern component_tag<TLight> v_lite35_tag;
extern component_tag<TLight> v_lite36_tag;
extern component_tag<TSound> tabc39wave_tag;
extern component_tag<TSound> tabc40wave_tag;
extern component_tag<TSound> tabc41wave_tag;
extern component_tag<TSound> tabc42wave_tag;
extern component_tag<TKickout> a_kout2_tag;
extern component_tag<TLight> v_lite46_tag;
extern component_tag<TLight> v_lite71_tag;
extern component_tag<TLight> v_lite72_tag;
extern component_tag<TLight> v_lite73_tag;
extern component_tag<TLight> v_lite74_tag;
extern component_tag<TLight> v_lite75_tag;
extern component_tag<TLight> v_lite76_tag;
extern component_tag<TSink> a_sink15_tag;
extern component_tag<TSink> a_sink18_tag;
extern component_tag<TSink> a_sink13_tag;
extern component_tag<TSink> a_sink17_tag;
extern component_tag<TSink> a_sink9_tag;
extern component_tag<TSink> a_sink10_tag;
extern component_tag<TSink> a_sink16_tag;
extern component_tag<TLight> v_lite48_tag;
extern component_tag<TLight> v_lite49_tag;
extern component_tag<TLight> v_lite50_tag;
extern component_tag<TLightRollover> v_wlite96_tag;
extern component_tag<TLightRollover> v_wlite97_tag;
extern component_tag<TLightRollover> v_wlite98_tag;
extern component_tag<TLightRollover> v_wlite99_tag;
extern component_tag<TTripwire> s_trip6_tag;
extern component_tag<TTripwire> s_trip5_tag;
extern component_tag<TTripwire> s_trip3_tag;
extern component_tag<TTripwire> s_trip4_tag;
extern component_tag<TTripwire> s_trip8_tag;
extern component_tag<TTripwire> s_trip9_tag;
extern component_tag<TLightGroup> prison_window_light_group_tag;
extern component_tag<TLightGroup> watch_window_light_group_tag;
extern component_tag<TLightGroup> gate_window_light_group_tag;
extern component_tag<TSound> tabc04wave_tag;
extern component_tag<TSound> tabc10wave_tag;
extern component_tag<TSound> tabc07wave_tag;
extern component_tag<TSound> tabc51wave_tag;
extern component_tag<TLight> v_lite28_tag;
extern component_tag<TLight> v_lite29_tag;
extern component_tag<TLight> v_lite30_tag;
extern component_tag<TKickout> a_kout3_tag;
extern component_tag<TLight> v_lite31_tag;
extern component_tag<TLight> v_lite32_tag;
extern component_tag<TLightSweeps> light_sweeps_tag;
extern component_tag<TTimer> mode_timer_tag;
extern component_tag<TLightGroup> gem_light_group_tag;
extern component_tag<TLight> v_lite88_tag;
extern component_tag<TLight> v_lite89_tag;
extern component_tag<TLight> v_lite90_tag;
extern component_tag<TLight> v_lite91_tag;
extern component_tag<TLight> v_lite92_tag;
extern component_tag<TLight> v_lite93_tag;
extern component_tag<TLight> v_lite94_tag;
extern component_tag<TLight> v_lite95_tag;
extern component_tag<TLight> v_lite117_tag;
extern component_tag<TSound> tabc43wave_tag;
extern component_tag<TSound> tabc44wave_tag;
extern component_tag<TSound> tabc46wave_tag;
extern component_tag<TSound> tabc47wave_tag;
extern component_tag<TSound> tabc48wave_tag;
extern component_tag<TSound> tabc50wave_tag;
extern component_tag<TSound> tabc53wave_tag;
extern component_tag<TSound> tabc03wave_tag;
extern component_tag<TSound> tabc55wave_tag;
extern component_tag<TSound> tabc56wave_tag;
extern component_tag<TSound> tabc57wave_tag;
extern component_tag<TSound> tabc58wave_tag;
extern component_tag<TSound> tabc61wave_tag;
extern component_tag<TLight> v_lite77_tag;
extern component_tag<TLight> v_lite78_tag;
extern component_tag<TLight> v_lite79_tag;
extern component_tag<TLight> v_lite125_tag;
extern component_tag<TLight> v_lite126_tag;
extern component_tag<TSound> tabc05wave_tag;
extern component_tag<TSound> tabc06wave_tag;
extern component_tag<TSound> tabc08wave_tag;
extern component_tag<TSound> tabc20wave_tag;
extern component_tag<TSound> tabc22wave_tag;
extern component_tag<TSound> tabc09wave_tag;
extern component_tag<TSound> tabc35wave_2_tag;
// references to the linked components
extern TKickback*& a_kick1;
extern TLightGroup*& outlane_kicker_light_group;
extern TSound*& tabc24wave;
extern TDiverter*& a_dvtraa;
extern TDiverter*& a_dvtrab;
extern TDiverter*& a_dvtrba;
extern TDiverter*& a_dvtrbb;
extern TDiverter*& a_dvtrca;
extern TDiverter*& a_dvtrcb;
extern TDiverter*& a_dvtrda;
extern TDiverter*& a_dvtrdb;
extern TBlocker*& a_bloce;
extern TBlocker*& a_blocf;
extern TLight*& v_lite47;
extern TBumper*& a_bump1;
extern TBumper*& a_bump2;
extern TBumper*& a_bump3;
extern TBumper*& a_bump4;
extern TWall*& a_rebo1;
extern TWall*& a_rebo2;
extern TLight*& v_lite120;
extern TLight*& v_lite121;
extern TTextBox*& info_text_box;
extern TDrain*& drain;
extern TPlunger*& s_plunger1;
extern TLight*& v_lite40;
extern TOneway*& s_oneway2;
extern TSoloTarget*& a_spot4;
extern TSoloTarget*& a_spot5;
extern TSoloTarget*& a_spot6;
extern TSoloTarget*& a_spot7;
extern TSoloTarget*& a_spot22;
extern TLight*& v_lite3;
extern TLight*& v_lite4;
extern TLight*& v_lite5;
extern TLight*& v_lite6;
extern TLight*& v_lite7;
extern TLightGroup*& spell_spot_light_group;
extern TSound*& tabc35wave;
extern TSoloTarget*& a_spot3;
extern TSoloTarget*& a_spot2;
extern TSoloTarget*& a_spot1;
extern TLight*& v_lite8;
extern TLight*& v_lite9;
extern TLight*& v_lite10;
extern TLight*& v_lite22;
extern TLight*& v_lite23;
extern TLight*& v_lite24;
extern TLight*& v_lite25;
extern TLight*& v_lite26;
extern TLight*& v_lite27;
extern TLightGroup*& dragon_letter_light_group;
extern TSoloTarget*& a_spot10;
extern TSoloTarget*& a_spot8;
extern TSoloTarget*& a_spot14;
extern TSoloTarget*& a_spot12;
extern TLight*& v_lite14;
extern TLight*& v_lite13;
extern TLight*& v_lite12;
extern TLight*& v_lite11;
extern TLightGroup*& gargoyle_spot_light_group;
extern TBlocker*& a_rocka;
extern TBlocker*& a_rockb;
extern TSoloTarget*& a_spot27;
extern TLight*& v_lite21;
extern TSound*& tabc96wave;
extern TLight*& v_lite57;
extern TSound*& tabc21wave;
extern TSoloTarget*& a_spot19;
extern TSoloTarget*& a_spot26;
extern TSoloTarget*& a_spot28;
extern TLight*& v_lite16;
extern TLight*& v_lite17;
extern TLight*& v_lite18;
extern TBlocker*& a_sword;
extern TLight*& v_lite58;
extern TLight*& v_lite59;
extern TLight*& v_lite60;
extern TLight*& v_lite61;
extern TLight*& v_lite62;
extern TLightGroup*& dragon_fire_light_group;
extern TSink*& a_sink19;
extern TBlocker*& a_jaw;
extern TLight*& v_lite113;
extern TLight*& v_lite114;
extern TLight*& v_lite115;
extern TLightGroup*& dragon_mouth_light_group;
extern TLight*& v_lite119;
extern TRamp*& a_ramp107;
extern TWall*& s_bwal170;
extern TSound*& tabc13wave;
extern TSoloTarget*& a_spot30;
extern TSoloTarget*& a_spot31;
extern TLight*& v_lite20;
extern TLight*& v_lite19;
extern TBlocker*& a_center_post;
extern TSink*& a_sink1;
extern TSink*& a_sink2;
extern TLight*& v_lite63;
extern TLight*& v_lite64;
extern TLight*& v_lite66;
extern TLight*& v_lite65;
extern TSound*& tabc16wave;
extern TSink*& a_sink4;
extern TLightGroup*& well_light_group;
extern TRamp*& a_ramp103;
extern TLight*& v_lite67;
extern TSound*& tabc18wave;
extern TRamp*& a_ramp6;
extern TLight*& v_lite70;
extern TOneway*& s_oneway5;
extern TLight*& v_lite68;
extern TAccelerator*& a_accl1;
extern TTripwire*& s_trip1;
extern TTripwire*& s_trip2;
extern TLight*& v_lite69;
extern TRollover*& a_roll7;
extern TRollover*& a_roll5;
extern TRollover*& a_roll2;
extern TRollover*& a_roll6;
extern TLight*& v_lite56;
extern TRollover*& a_roll1;
extern TLight*& v_lite55;
extern THole*& s_hole118;
extern TLightGroup*& dragon_egg_light_group;
extern TSound*& tabc27wave;
extern TSound*& tabc32wave;
extern TSound*& tabc33wave;
extern TSound*& tabc34wave;
extern TRampExit*& s_rexit19;
extern TRampExit*& s_rexit18;
extern TFlipper*& a_flip1;
extern TFlipper*& a_flip3;
extern TFlipper*& a_flip2;
extern TKickout*& a_kout1;
extern TLight*& v_lite37;
extern TLight*& v_lite38;
extern TLight*& v_lite39;
extern TLight*& v_lite45;
extern TLight*& v_lite33;
extern TLight*& v_lite34;
extern TLight*& v_lite35;
extern TLight*& v_lite36;
extern TSound*& tabc39wave;
extern TSound*& tabc40wave;
extern TSound*& tabc41wave;
extern TSound*& tabc42wave;
extern TKickout*& a_kout2;
extern TLight*& v_lite46;
extern TLight*& v_lite71;
extern TLight*& v_lite72;
extern TLight*& v_lite73;
extern TLight*& v_lite74;
extern TLight*& v_lite75;
extern TLight*& v_lite76;
extern TSink*& a_sink15;
extern TSink*& a_sink18;
extern TSink*& a_sink13;
extern TSink*& a_sink17;
extern TSink*& a_sink9;
extern TSink*& a_sink10;
extern TSink*& a_sink16;
extern TLight*& v_lite48;
extern TLight*& v_lite49;
extern TLight*& v_lite50;
extern TLightRollover*& v_wlite96;
extern TLightRollover*& v_wlite97;
extern TLightRollover*& v_wlite98;
extern TLightRollover*& v_wlite99;
extern TTripwire*& s_trip6;
extern TTripwire*& s_trip5;
extern TTripwire*& s_trip3;
extern TTripwire*& s_trip4;
extern TTripwire*& s_trip8;
extern TTripwire*& s_trip9;
extern TLightGroup*& prison_window_light_group;
extern TLightGroup*& watch_window_light_group;
extern TLightGroup*& gate_window_light_group;
extern TSound*& tabc04wave;
extern TSound*& tabc10wave;
extern TSound*& tabc07wave;
extern TSound*& tabc51wave;
extern TLight*& v_lite28;
extern TLight*& v_lite29;
extern TLight*& v_lite30;
extern TKickout*& a_kout3;
extern TLight*& v_lite31;
extern TLight*& v_lite32;
extern TLightSweeps*& light_sweeps;
extern TTimer*& mode_timer;
extern TLightGroup*& gem_light_group;
extern TLight*& v_lite88;
extern TLight*& v_lite89;
extern TLight*& v_lite90;
extern TLight*& v_lite91;
extern TLight*& v_lite92;
extern TLight*& v_lite93;
extern TLight*& v_lite94;
extern TLight*& v_lite95;
extern TLight*& v_lite117;
extern TSound*& tabc43wave;
extern TSound*& tabc44wave;
extern TSound*& tabc46wave;
extern TSound*& tabc47wave;
extern TSound*& tabc48wave;
extern TSound*& tabc50wave;
extern TSound*& tabc53wave;
extern TSound*& tabc03wave;
extern TSound*& tabc55wave;
extern TSound*& tabc56wave;
extern TSound*& tabc57wave;
extern TSound*& tabc58wave;
extern TSound*& tabc61wave;
extern TLight*& v_lite77;
extern TLight*& v_lite78;
extern TLight*& v_lite79;
extern TLight*& v_lite125;
extern TLight*& v_lite126;
extern TSound*& tabc05wave;
extern TSound*& tabc06wave;
extern TSound*& tabc08wave;
extern TSound*& tabc20wave;
extern TSound*& tabc22wave;
extern TSound*& tabc09wave;
extern TSound*& tabc35wave_2;

// ---- functions shared between the dragon_*.cpp files; each is defined in the file of its rule area
void AddSuperJackpot(int n);
void table_add_extra_ball(float t);
float AwardJackpot(float value, char showText);
int CheckMode15Start();
void ReleaseCapturedBall(component_tag_base& sink, float value);
void ModeStartLights();
void QuestEndCleanup();
void GemCollected(unsigned bits);
void AwardRandomSpell();
void AwardSpell(int n);
int PickUnlitSpell();
void ResetSpells();
void AddDragonEggs(int n);
void AwardNextWeapon();
void AwardRandomArmor();
void AwardArmorKickback();
void CloseDragonJaw(int n);
void CancelQuestSelection();
int SelectRandomQuest();
int StartSelectedQuest();
void CompleteQuest();
void SetSwordState(int state);
void UpdateTowerLocks();
void ResetTowerLocks();
void Mode0GameOverController(int code);
void Mode2WaitQuestController(int code);
void Mode3Controller(int code, TPinballComponent* caller);
void Mode4Controller(int code, TPinballComponent* caller);
void Mode5Controller(int code, TPinballComponent* caller);
void Mode6Controller(int code, TPinballComponent* caller);
void Mode7Controller(int code, TPinballComponent* caller);
void Mode8Controller(int code, TPinballComponent* caller);
void Mode9Controller(int code, TPinballComponent* caller);
void Mode10LaunchBallController(int code, TPinballComponent* caller);
void Mode11BonusController(int code, TPinballComponent* caller);
void Mode12EggShopController(int code, TPinballComponent* caller);
void Mode13Controller(int code);
void Mode14Controller(int code);
void Mode15Controller(int code);
// ---- provided by dragon_rules.cpp: thin forwards to the active DragonRules / ft::FtRules
void MissionControl(int code, TPinballComponent* caller);   // 0x42c020
void MissionControlSwitch(int mode, int code, TPinballComponent* caller);   // 0x42c120
void UnselectModeController();   // 0x42c000 (ft_common.cpp)
void ScorePart(TPinballComponent* comp, int index);   // 0x4230a0 (ft_common.cpp)
void ScoreValue(TPinballComponent* comp, int score);   // 0x4230f0 (ft_common.cpp)
void SpecialAddScore(TPinballComponent* comp, int score);   // 0x423130 (ft_common.cpp)
void AddJackpot(int n);   // 0x4238a0 (ft_common.cpp)
bool CheckBallInControlBounds(TBall* ball, TPinballComponent* part);   // 0x42c290 (ft_common.cpp)
void SetMode(int mode);   // info_text_box MessageField
int Mode();
void PlayTrack(int handle);   // midi::play_track(handle, true): music slot handles are DragonState members
}

namespace dragon {
DragonRules& R();    // the active rules (aborts when control::Rules is not DragonRules)
DragonState& S();    // R().St
}
