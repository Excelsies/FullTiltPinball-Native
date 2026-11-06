#pragma once
#include "score.h"
#include "TPinballComponent.h"
#include "TTextBoxMessage.h"

class TTextBox :
	public TPinballComponent
{
public:
	int OffsetX;
	int OffsetY;
	int Width;
	int Height;
	int Timer;
	gdrv_bitmap8* BgBmp;
	TTextBoxMessage* CurrentMessage;
	TTextBoxMessage* PreviousMessage;
	// Overlay channel (Full Tilt credits / quote ticker): drawn over the queue until OverlayEnd.
	int OverlayState; // 0 none, 2 pending (not drawn yet), 1 shown
	int OverlayFont; // score::msg_fonts slot
	int OverlayTimer;
	char OverlayText[200];
	// Full Tilt queue mode (pb::IsFullTiltTable() at construction). Space Cadet keeps upstream's queue.
	bool FtMode;

	TTextBox(TPinballTable* table, int groupIndex);
	~TTextBox() override;
	int Message(MessageCode code, float value) override;
	void Clear(bool lowPriorityOnly = false);
	void Display(const char* text, float time, bool lowPriority = false);
	// font = score::msg_fonts slot the message is drawn with; Space Cadet only uses the overload above (slot 0).
	void Display(const char* text, float time, int font, bool lowPriority = false);
	void Overlay(const char* text, float time, int font);
	void OverlayEnd();
	void DrawImGui();
	// FT +0x5e (queue head non-null): "a message is on screen"; read by the Dragon / Skulduggery mode controllers.
	bool HasPendingMessage() const { return CurrentMessage != nullptr; }
#ifdef FT_TEST_HOOK
	bool HookGet(const std::string& field, double& out) const override;
#endif

private:
	struct LayoutResult
	{
		char *Start, *End;
		int Width;
	};

	static void TimerExpired(int timerId, void* caller);
	static void OverlayExpired(int timerId, void* caller);

	void Draw();
	// Full Tilt queue (FT 0x41de40 Display, 0x41dd30 scheduler, 0x41dce0 expiry, 0x41d8e0 queue free, 0x41db00 draw).
	void FtDisplay(const char* text, float time, int font, bool lowPriority);
	bool FtSchedule();
	void FtExpired();
	void FtFreeQueue();
	void FtDraw();
	void DrawText(char* text, int fontSlot);
	void Redraw();
	LayoutResult LayoutTextLine(char* textStart, const score_msg_font_type* font) const;
#ifdef FT_TEST_HOOK
	int ControlSent46; // ControlTimerExpired sends by the FT scheduler (hook field ctrl46)
#endif
};
