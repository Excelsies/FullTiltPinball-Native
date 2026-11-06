#pragma once
#include "gdrv.h"

struct scoreStruct
{
	int Score;
	bool DirtyFlag;
	gdrv_bitmap8* BackgroundBmp;
	int OffsetX;
	int OffsetY;
	int Width;
	int Height;
	gdrv_bitmap8* CharBmp[10];
};

struct score_msg_font_type
{
	int GapWidth;
	int Height;
	gdrv_bitmap8* Chars[128]{};
};


class score
{
public:
	// Message font slots: 0 = pbmsg_ft, 1 = font2 (Full Tilt tables). Space Cadet only ever uses slot 0.
	static const int MsgFontCount = 2;
	static score_msg_font_type* msg_fonts[MsgFontCount];
	static int init();
	static scoreStruct* create(LPCSTR fieldName, gdrv_bitmap8* renderBgBmp);
	static scoreStruct* dup(scoreStruct* score, int scoreIndex);
	static void load_msg_font(LPCSTR lpName, int slot = 0);
	static void unload_msg_font();
	static void erase(scoreStruct* score, int blitFlag);
	static void set(scoreStruct* score, int value);
	static void update(scoreStruct* score);
	static void string_format(int score, char* str);
};
