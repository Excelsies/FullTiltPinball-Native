#pragma once
class TTextBoxMessage
{
public:
	TTextBoxMessage* NextMessage;
	char* Text;
	float Time;
	int EndTicks;
	bool LowPriority;
	int Font; // score::msg_fonts slot the message is drawn with

	TTextBoxMessage(const char* text, float time, bool lowPriority, int font = 0);
	~TTextBoxMessage();
	float TimeLeft() const;
	void Refresh(float time);
};
