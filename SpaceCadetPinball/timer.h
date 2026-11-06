#pragma once

struct timer_struct
{
	int TargetTime;
	void* Caller;
	void (* Callback)(int, void*);
	timer_struct* NextTimer;
	int TimerId;
};

class timer
{
public:
	static int init(int count);
	static void uninit();
	static int kill(int timerId);
	static int kill(void (*callback)(int, void*));
	static int set(float time, void* caller, void (* callback)(int, void*));
	static int check();
	// Seconds until the timer fires (0.0 when already due), -1.0 when no active timer has that id (FT 0x41f880).
	static float get_time_left(int timerId);

private:
	static int SetCount;
	static timer_struct* ActiveList;
	static int MaxCount;
	static int Count;
	static timer_struct* FreeList;
	static timer_struct* TimerBuffer;
};
