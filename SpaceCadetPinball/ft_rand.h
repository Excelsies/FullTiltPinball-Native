#pragma once
#include <cstdint>

// One random stream for engine and rules, so that every draw happens in the same order as in the original.
// Full Tilt tables: MSVC rand() LCG, state 1 at ResetForTable(). Space Cadet: std::rand / std::srand.
//   Seed(seed)    sets the LCG state (Full Tilt) or calls std::srand (Space Cadet)
//   Int()         next value, 0..32767
//   Float()       Int() / 32767 (Full Tilt); upstream's Int() / RAND_MAX formula (Space Cadet)
//   DrawCount()   Int()/Float() draws since process start
//   LastValue()   result of the last Int()
namespace ft_rand
{
	void Seed(uint32_t seed);
	int Int();
	float Float();
	void ResetForTable();
	uint64_t DrawCount();
	int LastValue();

	// MSVC rand() LCG step: state = state * 214013 + 2531011, result = bits 16..30.
	inline uint32_t MsvcNext(uint32_t& state)
	{
		state = state * 0x343fdu + 0x269ec3u;
		return (state >> 16) & 0x7fffu;
	}
}
