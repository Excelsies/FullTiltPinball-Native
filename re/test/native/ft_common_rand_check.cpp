// Standalone check for the ft::Rand / ft::RandIndex wrappers of rules/ft_common.h.
// No game data. Build and run from the repo root:
//   g++ -std=c++11 -I SpaceCadetPinball $(sdl2-config --cflags) -include pch.h \
//       re/test/native/ft_common_rand_check.cpp SpaceCadetPinball/ft_rand.cpp -o <out>, then run <out>
// Expected values are literals computed from the MSVC LCG (state*0x343fd + 0x269ec3, bits 16..30), not recomputed here.
#include "pb.h"
#include "ft_rand.h"
#include "rules/ft_common.h"
#include <cstdio>

bool pb::IsFullTiltTable() { return true; }

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main()
{
	ft_rand::Seed(1);
	const int raw[5] = {41, 18467, 6334, 26500, 19169};
	for (int i = 0; i < 5; ++i)
		CHECK(ft::Rand() == raw[i]);
	CHECK(ft_rand::DrawCount() == 5);
	CHECK(ft_rand::LastValue() == 19169);

	// draws 6..13 are 15724, 11478, 29358, 26962, 24464, 5705, 28145, 23281
	const int n[8] = {2, 3, 4, 5, 6, 10, 150, 7};
	const int idx[8] = {0, 1, 3, 4, 4, 1, 128, 4};
	for (int i = 0; i < 8; ++i)
		CHECK(ft::RandIndex(n[i]) == idx[i]);
	CHECK(ft_rand::DrawCount() == 13);
	CHECK(ft_rand::LastValue() == 23281);

	std::printf(g_fail ? "FAIL (%d)\n" : "PASS\n", g_fail);
	return g_fail ? 1 : 0;
}
