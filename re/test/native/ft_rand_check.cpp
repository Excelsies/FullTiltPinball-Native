// Standalone check for ft_rand. No game data.
// Build and run (from the repo root; SDL2 headers needed because ft_rand.cpp includes pch.h):
//   g++ -std=c++11 -I SpaceCadetPinball $(sdl2-config --cflags) re/test/native/ft_rand_check.cpp SpaceCadetPinball/ft_rand.cpp -o <out>, then run <out>
#include "pch.h"
#include "pb.h"
#include "ft_rand.h"
#include <cstdlib>

static bool g_fullTilt = false;
bool pb::IsFullTiltTable() { return g_fullTilt; }

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main()
{
	// Full Tilt path: MSVC LCG, state 1 -> 41, 18467, 6334, 26500, 19169 (literal values).
	g_fullTilt = true;
	ft_rand::Seed(1);
	const int expected[5] = {41, 18467, 6334, 26500, 19169};
	const uint64_t base = ft_rand::DrawCount();
	for (int i = 0; i < 5; ++i)
		CHECK(ft_rand::Int() == expected[i]);
	CHECK(ft_rand::DrawCount() - base == 5);
	CHECK(ft_rand::LastValue() == 19169);

	// MsvcNext is the same LCG.
	uint32_t st = 1;
	CHECK(ft_rand::MsvcNext(st) == 41u);
	CHECK(ft_rand::MsvcNext(st) == 18467u);

	// ResetForTable restarts at state 1.
	ft_rand::ResetForTable();
	CHECK(ft_rand::Int() == 41);

	// Float = Int() / 32767.
	ft_rand::ResetForTable();
	float f = ft_rand::Float();
	CHECK(std::fabs(f - 0.0012513f) < 1e-6f);   // 41 / 32767
	CHECK(ft_rand::LastValue() == 41);
	CHECK(std::fabs(RandFloat() - 18467.0 / 32767.0) < 1e-6);   // RandFloat routes through ft_rand

	// Seed(n) sets the state: a different seed gives a different first value.
	ft_rand::Seed(2);
	CHECK(ft_rand::Int() == 45); // state 2 -> (2*0x343fd+0x269ec3)>>16 & 0x7fff
	// Space Cadet path: std::rand / srand unchanged.
	g_fullTilt = false;
	std::srand(7);
	int a[4];
	for (int& v : a) v = std::rand();
	double fa = std::rand() / static_cast<double>(RAND_MAX);
	ft_rand::Seed(7);
	const uint64_t b2 = ft_rand::DrawCount();
	for (int v : a) CHECK(ft_rand::Int() == v);
	CHECK(ft_rand::Float() == static_cast<float>(fa));
	CHECK(ft_rand::DrawCount() - b2 == 5);
	ft_rand::ResetForTable();   // must not disturb the std::rand stream
	std::srand(7);
	for (int v : a) CHECK(std::rand() == v);

	std::printf(g_fail ? "ft_rand_check: FAIL (%d)\n" : "ft_rand_check: PASS\n", g_fail);
	return g_fail ? 1 : 0;
}
