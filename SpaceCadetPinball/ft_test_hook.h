#pragma once

class TPinballComponent;
class TTextBox;

// Native test hook. Bodies live in ft_test_hook.cpp inside #ifdef FT_TEST_HOOK;
// without it every function is an inline no-op. TextCode(-1) ends the code context.
namespace ft_test_hook
{
#ifdef FT_TEST_HOOK
	void Init();
	bool Active();
	bool DefersReplay();
	float FrameDt(float measuredMs);
	bool HoldFrame();
	void BeforeFrame();
	void AfterFrame();
	void AfterRender(SDL_Renderer* renderer);
	void SoundEvent(int id, TPinballComponent* source, const char* info);
	void TrackEvent(int track, bool replay);
	void TextLog(TTextBox* box, int op, const char* text, float time, int font, bool lowPriority);
	void TextCode(int ftCode);
	void TextFetched(int id);
	void Shutdown();
#else
	inline void Init() {}
	inline bool Active() { return false; }
	inline bool DefersReplay() { return false; }
	inline float FrameDt(float measuredMs) { return measuredMs; }
	inline bool HoldFrame() { return false; }
	inline void BeforeFrame() {}
	inline void AfterFrame() {}
	inline void AfterRender(SDL_Renderer*) {}
	inline void SoundEvent(int, TPinballComponent*, const char*) {}
	inline void TrackEvent(int, bool) {}
	inline void TextLog(TTextBox*, int, const char*, float, int, bool) {}
	inline void TextCode(int) {}
	inline void TextFetched(int) {}
	inline void Shutdown() {}
#endif
}
