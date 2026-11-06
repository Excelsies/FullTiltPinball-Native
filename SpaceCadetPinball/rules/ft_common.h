#pragma once
#include "../ft_rand.h"            // the one LCG state, shared by engine and rules
#include "../table_rules.h"        // TableRules
#include "../TPinballComponent.h"  // MessageCode, TPinballComponent
class TLight;                      // forward declarations: the header uses pointers / references only
class TTextBox;
class TBall;
struct component_tag_base;         // control.h (upstream declares it a struct)
namespace ft {
inline int Rand() { return ft_rand::Int(); }                         // a raw draw, 0..32767
inline int RandIndex(int n) { return (ft_rand::Int() * n) >> 15; }   // the original's (r*n + ((r*n>>31)&0x7fff)) >> 15; r*n >= 0
class FtRules : public TableRules {
public:
    void Handler(MessageCode code, TPinballComponent* caller) override;          // 0x423060 / 0x423080
protected:
    struct Common {                   // per-table addresses in the comments of each table's rules header
        int LinkFailed;               // D 0x439b08 / P 0x43aba4
        TPinballComponent* LastScorer;// D 0x434d20 / P 0x435d44
        int BallSave;                 // D 0x434d2c / P 0x435d90 (3..0)
        int TickerTimer, QuoteFlag;   // D 0x43ba98, 0x43ba9c / P 0x43cfe0, 0x43cfe4
        const char* TickerCursor;     // D 0x434cfc / P 0x435cfc, points into table_text::GetData's buffer
    } C{};
    TLight*   BallSaveLight = nullptr;     // D v_lite40 / P v_lite569, set in MakeLinks
    TLight*   ReboundLight[2] = {nullptr, nullptr};  // RebounderControl1 / 2 light: D v_lite120 / v_lite121, P v_lite84 / v_lite85, set in MakeLinks
    TTextBox* InfoBox = nullptr;           // engine info_text_box (D 0x439d1c / P 0x43ad3c)
    TPinballComponent* MakeComponentLink(component_tag_base& tag);               // 0x422ef0 / 0x422f10
    int ScorePart(TPinballComponent* comp, int index);                            // 0x4230a0 / 0x4232c0
    int ScoreValue(TPinballComponent* comp, int score);                           // 0x4230f0 / 0x423310
    void SpecialAddScore(TPinballComponent* comp, int score);                     // 0x423130 / 0x423350
    void AddJackpot(int n);                                                       // 0x4238a0 / 0x423650
    void UnselectModeController();                                                // 0x42c000 / 0x42d830
    bool CheckBallInControlBounds(TBall* ball, TPinballComponent* part);          // 0x42c290 / 0x42daa0
    void TickerStart(bool fromStart);                                             // 0x422a80 / 0x422a90
    void TickerStop();                                                            // 0x422b50 / 0x422b60
    void NoteScorer(TPinballComponent* comp);                                     // the inlined "last scorer gets 0x4d" prologue of ScorePart / ScoreValue / SpecialAddScore
    void Rebound(int which, MessageCode code, TPinballComponent* caller);        // RebounderControl1 (0) / 2 (1)
    static void TickerExpired(int timerId, void* caller);                         // timer callback: caller != nullptr = restart the text
    friend void BallSaveLightControl(MessageCode, TPinballComponent*);
    friend void LaunchOneWayControl(MessageCode, TPinballComponent*);
    friend void RebounderControl1(MessageCode, TPinballComponent*);
    friend void RebounderControl2(MessageCode, TPinballComponent*);
    virtual void MissionControl(int code, TPinballComponent* caller) = 0;         // per table (dragon_rules.cpp / pirates_rules.cpp)
    virtual void SetMode(int m) = 0;  virtual int Mode() const = 0;               // per table (dragon_rules.cpp / pirates_rules.cpp)
};
FtRules& Cur();                       // the active rules (control::Rules), for control functions
void BallSaveLightControl(MessageCode, TPinballComponent*);                       // 0x424870 / 0x426080
void LaunchOneWayControl(MessageCode, TPinballComponent*);                        // 0x424590 / 0x42a3b0
void RebounderControl1(MessageCode, TPinballComponent*);                          // 0x424cc0 / 0x427350
void RebounderControl2(MessageCode, TPinballComponent*);                          // 0x424cf0 / 0x427380
void NullControl(MessageCode, TPinballComponent*);                                // P 0x422a80 (`ret 8`)
}
