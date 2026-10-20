// RandomStream.cpp - FRandomStream on std::mt19937_64 with the harness [0,1) mapping (02_Architecture 14.1).
#include "Math/RandomStream.h"

namespace PhysicsTest
{
	void FRandomStream::Initialize(const int32 InSeed)
	{
		InitialSeed = InSeed;
		Engine.seed(static_cast<uint64>(static_cast<uint32>(InSeed)));
	}

	void FRandomStream::Reset()
	{
		Initialize(InitialSeed);
	}

	double FRandomStream::GetFraction()
	{
		return static_cast<double>(Engine() >> 11) * 0x1.0p-53;
	}

	float FRandomStream::FRand()
	{
		return static_cast<float>(Engine() >> 40) * 0x1.0p-24f;
	}

	uint32 FRandomStream::GetUnsignedInt()
	{
		return static_cast<uint32>(Engine() >> 32);
	}

	int32 FRandomStream::RandHelper(const int32 A)
	{
		if (A <= 0)
		{
			return 0;
		}
		const int32 Value = static_cast<int32>(GetFraction() * static_cast<double>(A));
		return Value < A ? Value : A - 1;
	}

	int32 FRandomStream::RandRange(const int32 Min, const int32 Max)
	{
		const int32 Range = (Max - Min) + 1;
		return Min + RandHelper(Range);
	}

	double FRandomStream::FRandRange(const double Min, const double Max)
	{
		return Min + (Max - Min) * GetFraction();
	}
}
