#include "pch.h"
#include "ft_rand.h"
#include "pb.h"

namespace
{
	constexpr double kInv32767 = 3.051850947599719e-05;
	uint32_t FtState = 1;
	uint64_t Draws = 0;
	int Last = 0;
}

void ft_rand::Seed(uint32_t seed)
{
	if (pb::IsFullTiltTable())
		FtState = seed;
	else
		std::srand(seed);
}

int ft_rand::Int()
{
	Last = pb::IsFullTiltTable() ? static_cast<int>(MsvcNext(FtState)) : std::rand();
	++Draws;
	return Last;
}

float ft_rand::Float()
{
	const int value = Int();
	if (pb::IsFullTiltTable())
		return static_cast<float>(value * kInv32767);
	return static_cast<float>(value / static_cast<double>(RAND_MAX));
}

void ft_rand::ResetForTable()
{
	FtState = 1;
}

uint64_t ft_rand::DrawCount()
{
	return Draws;
}

int ft_rand::LastValue()
{
	return Last;
}
