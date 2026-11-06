#include "pch.h"
#include "TTextBox.h"

#include "control.h"
#include "ft_test_hook.h"
#include "fullscrn.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "score.h"
#include "timer.h"


TTextBox::TTextBox(TPinballTable* table, int groupIndex) : TPinballComponent(table, groupIndex, true)
{
	OffsetX = 0;
	OffsetY = 0;
	Width = 0;
	Height = 0;
	BgBmp = render::background_bitmap;
	CurrentMessage = nullptr;
	PreviousMessage = nullptr;
	Timer = 0;
	OverlayState = 0;
	OverlayFont = 0;
	OverlayTimer = 0;
	OverlayText[0] = 0;
	FtMode = pb::IsFullTiltTable();
#ifdef FT_TEST_HOOK
	ControlSent46 = 0;
#endif

	if (groupIndex > 0)
	{
		/*Full tilt: text box dimensions index is offset by resolution*/
		int arrLength;
		auto dimensions = loader::query_iattribute(groupIndex + fullscrn::GetResolution(), 1500, &arrLength);
		OffsetX = dimensions[0];
		OffsetY = dimensions[1];
		Width = dimensions[2];
		Height = dimensions[3];
	}
}

TTextBox::~TTextBox()
{
	if (Timer)
	{
		if (Timer != -1)
			timer::kill(Timer);
		Timer = 0;
	}
	if (OverlayTimer)
	{
		timer::kill(OverlayTimer);
		OverlayTimer = 0;
	}
	while (CurrentMessage)
	{
		TTextBoxMessage* message = CurrentMessage;
		TTextBoxMessage* nextMessage = message->NextMessage;
		delete message;
		CurrentMessage = nextMessage;
	}
}

int TTextBox::Message(MessageCode code, float value)
{
	// FT 0x41e110 = base per-player MessageField handling (PlayerChanged swaps, Reset zeroes); the queue is not per player.
	if (FtMode)
		return TPinballComponent::Message(code, value);
	return 0;
}

void TTextBox::TimerExpired(int timerId, void* caller)
{
	auto tb = static_cast<TTextBox*>(caller);
	if (tb->FtMode)
	{
		tb->FtExpired();
		return;
	}
	TTextBoxMessage* message = tb->CurrentMessage;
	tb->Timer = 0;
	if (message)
	{
		TTextBoxMessage* nextMessage = message->NextMessage;
		delete message;
		tb->CurrentMessage = nextMessage;
		tb->Draw();
		control::handler(MessageCode::ControlTimerExpired, tb);
	}
}

void TTextBox::Clear(bool lowPriorityOnly)
{
	ft_test_hook::TextLog(this, 1, nullptr, 0.0f, 0, lowPriorityOnly);
	// An overlay owns the box: leave its pixels alone (Full Tilt clear 0x41d930).
	if (OverlayState == 0)
	{
		gdrv_bitmap8* bmp = BgBmp;
		if (bmp)
			gdrv::copy_bitmap(
				render::vscreen,
				Width,
				Height,
				OffsetX,
				OffsetY,
				bmp,
				OffsetX,
				OffsetY);
		else
			gdrv::fill_bitmap(render::vscreen, Width, Height, OffsetX, OffsetY, 0);
	}
	if (Timer)
	{
		if (Timer != -1)
			timer::kill(Timer);
		Timer = 0;
	}
	while (CurrentMessage && (!lowPriorityOnly || CurrentMessage->LowPriority))
	{
		auto message = CurrentMessage;
		CurrentMessage = message->NextMessage;
		delete message;
	}
	if (CurrentMessage)
		Draw();
}

void TTextBox::Display(const char* text, float time, bool lowPriority)
{
	Display(text, time, 0, lowPriority);
}

void TTextBox::Display(const char* text, float time, int font, bool lowPriority)
{
	ft_test_hook::TextLog(this, 0, text, time, font, lowPriority);
	SDL_assert_release(font >= 0 && font < score::MsgFontCount);
	if (!text)
		return;
	if (FtMode)
	{
		FtDisplay(text, time, font, lowPriority);
		return;
	}

	if (CurrentMessage && !strcmp(text, PreviousMessage->Text) && score::msg_fonts[PreviousMessage->Font] == score::msg_fonts[font])
	{
		PreviousMessage->Refresh(time);
		if (PreviousMessage == CurrentMessage)
		{
			if (Timer && Timer != -1)
				timer::kill(Timer);
			if (time == -1.0f)
				Timer = -1;
			else
				Timer = timer::set(time, this, TimerExpired);
		}
	}
	else
	{
		if (Timer == -1)
			Clear();

		auto message = new TTextBoxMessage(text, time, lowPriority, font);
		if (message->Text)
		{
			if (CurrentMessage)
				PreviousMessage->NextMessage = message;
			else
				CurrentMessage = message;
			PreviousMessage = message;
			if (Timer == 0)
				Draw();
		}
		else
		{
			delete message;
		}
	}
}

void TTextBox::DrawImGui()
{
	// Do nothing when using a font (the text will be rendered to VScreen in TTextBox::Draw)
	const char* shownText;
	if (OverlayState == 1)
	{
		if (score::msg_fonts[OverlayFont])
			return;
		shownText = OverlayText;
	}
	else
	{
		if (!CurrentMessage || score::msg_fonts[CurrentMessage->Font])
			return;
		shownText = CurrentMessage->Text;
	}

	char windowName[64];
	SDL_Rect rect;
	ImGuiWindowFlags window_flags =
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoInputs;

	rect.x = OffsetX;
	rect.y = OffsetY;
	rect.w = Width;
	rect.h = Height;

	rect = fullscrn::GetScreenRectFromPinballRect(rect);

	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(rect.x), static_cast<float>(rect.y)));
	ImGui::SetNextWindowSize(ImVec2(static_cast<float>(rect.w), static_cast<float>(rect.h)));

	// Use the pointer to generate a window unique name per text box
	snprintf(windowName, sizeof(windowName), "TTextBox_%p", static_cast<void*>(this));
	if (ImGui::Begin(windowName, nullptr, window_flags))
	{
		ImGui::SetWindowFontScale(fullscrn::GetScreenToPinballRatio());

		// ToDo: centered text in FT
		ImGui::PushStyleColor(ImGuiCol_Text, pb::TextBoxColor);
		ImGui::TextWrapped("%s", shownText);
		ImGui::PopStyleColor();
	}
	ImGui::End();
}

void TTextBox::Overlay(const char* text, float time, int font)
{
	SDL_assert_release(text && font >= 0 && font < score::MsgFontCount);
	strncpy(OverlayText, text, sizeof OverlayText - 2);
	OverlayText[sizeof OverlayText - 2] = 0;
	OverlayFont = font;
	OverlayState = 2;
	if (OverlayTimer)
	{
		timer::kill(OverlayTimer);
		OverlayTimer = 0;
	}
	if (time >= 0.0f)
		OverlayTimer = timer::set(0.25f, this, OverlayExpired);
	Redraw();
}

void TTextBox::OverlayEnd()
{
	if (OverlayTimer)
	{
		timer::kill(OverlayTimer);
		OverlayTimer = 0;
	}
	OverlayState = 0;
	if (CurrentMessage)
		Redraw();
}

void TTextBox::OverlayExpired(int timerId, void* caller)
{
	auto tb = static_cast<TTextBox*>(caller);
	tb->OverlayTimer = 0;
	tb->OverlayEnd();
}

// Draw() schedules the queue timer itself, so a second call must not find one running.
void TTextBox::Redraw()
{
	if (FtMode)
	{
		// FT 0x41e070 / 0x41e0e0 draw the head (0x41db00) and leave the queue timer alone.
		FtDraw();
		return;
	}
	if (Timer)
	{
		if (Timer != -1)
			timer::kill(Timer);
		Timer = 0;
	}
	Draw();
}

void TTextBox::Draw()
{
	// A shown overlay keeps its pixels; the queue is still scheduled underneath.
	if (OverlayState != 1)
	{
		auto bmp = BgBmp;
		if (bmp)
			gdrv::copy_bitmap(
				render::vscreen,
				Width,
				Height,
				OffsetX,
				OffsetY,
				bmp,
				OffsetX,
				OffsetY);
		else
			gdrv::fill_bitmap(render::vscreen, Width, Height, OffsetX, OffsetY, 0);
	}

	bool display = false;
	while (CurrentMessage)
	{
		if (CurrentMessage->Time == -1.0f)
		{
			if (!CurrentMessage->NextMessage)
			{
				Timer = -1;
				display = true;
				break;
			}
		}
		else if (CurrentMessage->TimeLeft() >= -2.0f)
		{
			Timer = timer::set(std::max(CurrentMessage->TimeLeft(), 0.25f), this, TimerExpired);
			display = true;
			break;
		}

		auto tmp = CurrentMessage;
		CurrentMessage = CurrentMessage->NextMessage;
		delete tmp;
	}

	if (OverlayState == 2)
	{
		OverlayState = 1;
		DrawText(OverlayText, OverlayFont);
	}
	else if (OverlayState == 0 && display)
		DrawText(CurrentMessage->Text, CurrentMessage->Font);
}

void TTextBox::DrawText(char* message, int fontSlot)
{
	auto Font = score::msg_fonts[fontSlot];
	if (!Font)
	{
		// Immediate mode drawing using system font is handled by TTextBox::DrawImGui
		return;
	}

	std::vector<LayoutResult> lines{};
	auto textHeight = 0;
	for (auto text = message; ; textHeight += Font->Height)
	{
		if (!text[0] || textHeight + Font->Height > Height)
			break;

		auto line = LayoutTextLine(text, Font);
		if (line.Start == line.End)
			break;
		lines.push_back(line);
		text = line.End;
	}

	// Textboxes in FT display texts centered
	auto offY = OffsetY;
	if (pb::FullTiltMode)
		offY += (Height - textHeight) / 2;
	for (auto line : lines)
	{
		auto offX = OffsetX;
		if (pb::FullTiltMode)
			offX += (Width - line.Width) / 2;
		for (auto text = line.Start; text < line.End; text++)
		{
			auto charBmp = Font->Chars[*text & 0x7F];
			if (charBmp)
			{
				auto height = charBmp->Height;
				auto width = charBmp->Width;
				if (render::background_bitmap)
					gdrv::copy_bitmap_w_transparency(render::vscreen, width, height, offX, offY, charBmp, 0,
					                                 0);
				else
					gdrv::copy_bitmap(render::vscreen, width, height, offX, offY, charBmp, 0, 0);
				offX += charBmp->Width + Font->GapWidth;
			}
		}
		offY += Font->Height;
	}
}

TTextBox::LayoutResult TTextBox::LayoutTextLine(char* textStart, const score_msg_font_type* Font) const
{
	auto lineWidth = 0, wordWidth = 0;
	char *wordBoundary = nullptr, *textEnd;
	for (textEnd = textStart; ; ++textEnd)
	{
		auto maskedChar = textEnd[0] & 0x7F;
		if (!maskedChar || maskedChar == '\n')
			break;

		auto charBmp = Font->Chars[maskedChar];
		if (!charBmp)
			continue;

		auto width = lineWidth + charBmp->Width + Font->GapWidth;
		if (width > Width)
		{
			if (wordBoundary)
			{
				textEnd = wordBoundary;
				lineWidth = wordWidth;
			}
			break;
		}
		if (maskedChar == ' ')
		{
			wordBoundary = textEnd;
			wordWidth = width;
		}
		lineWidth = width;
	}

	while ((*textEnd & 0x7F) == ' ')
		++textEnd;
	if ((*textEnd & 0x7F) == '\n')
		++textEnd;
	return LayoutResult{textStart, textEnd, lineWidth};
}

// FT 0x41d8e0: kill the timer, free every node, no repaint.
void TTextBox::FtFreeQueue()
{
	if (Timer)
	{
		if (Timer != -1)
			timer::kill(Timer);
		Timer = 0;
	}
	while (CurrentMessage)
	{
		auto message = CurrentMessage;
		CurrentMessage = message->NextMessage;
		delete message;
	}
	PreviousMessage = nullptr;
}

// FT 0x41dd30. Returns true when a message stays on screen (timer armed, or an untimed head).
bool TTextBox::FtSchedule()
{
	if (Timer && Timer != -1)
		timer::kill(Timer);
	Timer = 0;

	float left = 0.0f;
	if (CurrentMessage)
	{
		if (CurrentMessage->Time == -1.0f)
			return true;
		while (CurrentMessage)
		{
			left = static_cast<float>(CurrentMessage->EndTicks - pb::time_ticks) / 1000.0f;
			if (left > -2.0f)
				break;
			auto stale = CurrentMessage;
			CurrentMessage = stale->NextMessage;
			delete stale;
		}
	}
	if (!CurrentMessage)
	{
		PreviousMessage = nullptr;
#ifdef FT_TEST_HOOK
		ControlSent46++;
#endif
		control::handler(MessageCode::ControlTimerExpired, this);
		if (!CurrentMessage)
			Clear();
		return false;
	}
	if (left < 0.25f)
		left = 0.25f;
	Timer = timer::set(left, this, TimerExpired);
	return true;
}

// FT 0x41dce0.
void TTextBox::FtExpired()
{
	Timer = 0;
	if (CurrentMessage)
	{
		auto message = CurrentMessage;
		CurrentMessage = message->NextMessage;
		delete message;
	}
	if (FtSchedule())
		FtDraw();
}

// FT 0x41de40.
void TTextBox::FtDisplay(const char* text, float time, int font, bool lowPriority)
{
	if (CurrentMessage && !strcmp(text, PreviousMessage->Text) &&
		score::msg_fonts[PreviousMessage->Font] == score::msg_fonts[font])
	{
		if (CurrentMessage->Time == -1.0f && time == -1.0f)
			return;
		if (time != -1.0f)
		{
			PreviousMessage->Refresh(time);
			FtSchedule();
			return;
		}
	}
	// An untimed head is replaced by any message; an untimed message replaces the whole queue.
	if (CurrentMessage && CurrentMessage->Time == -1.0f)
		FtFreeQueue();
	if (time == -1.0f)
		FtFreeQueue();

	auto message = new TTextBoxMessage(text, time, lowPriority, font);
	if (!message->Text)
	{
		delete message;
		return;
	}
	if (CurrentMessage)
		PreviousMessage->NextMessage = message;
	else
		CurrentMessage = message;
	PreviousMessage = message;
	FtSchedule();
	FtDraw();
}

// FT 0x41db00: overlay first (pending -> shown, a shown overlay keeps its pixels), else the queue head.
void TTextBox::FtDraw()
{
	if (!CurrentMessage && OverlayState == 0)
		return;
	if (OverlayState == 1)
		return;
	auto bmp = BgBmp;
	if (bmp)
		gdrv::copy_bitmap(render::vscreen, Width, Height, OffsetX, OffsetY, bmp, OffsetX, OffsetY);
	else
		gdrv::fill_bitmap(render::vscreen, Width, Height, OffsetX, OffsetY, 0);
	if (OverlayState == 2)
	{
		OverlayState = 1;
		DrawText(OverlayText, OverlayFont);
	}
	else
		DrawText(CurrentMessage->Text, CurrentMessage->Font);
}

#ifdef FT_TEST_HOOK
bool TTextBox::HookGet(const std::string& field, double& out) const
{
	if (field == "pending")
		out = HasPendingMessage() ? 1 : 0;
	else if (field == "queue_len")
	{
		int n = 0;
		for (auto m = CurrentMessage; m; m = m->NextMessage)
			n++;
		out = n;
	}
	else if (field == "head_time")
		out = CurrentMessage ? CurrentMessage->Time : 0.0;
	else if (field == "armed")
		out = Timer != 0 ? 1 : 0;
	else if (field == "ctrl46")
		out = ControlSent46;
	else
		return TPinballComponent::HookGet(field, out);
	return true;
}
#endif
