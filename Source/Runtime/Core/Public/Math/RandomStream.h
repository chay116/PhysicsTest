// RandomStream.h - FRandomStream with UE's interface but a harness-defined generator (02_Architecture 14.1).
// Intentional difference: UE uses a 32-bit LCG; the harness uses std::mt19937_64 and its own [0,1) mapping so that
// scene generation is identical in every build of the harness. Default seed 0x5EED.
#pragma once

#include "CoreTypes.h"

#include <random>

namespace PhysicsTest
{
	class FRandomStream
	{
	public:
		static constexpr int32 DefaultSeed = 0x5EED;

		FRandomStream() { Initialize(DefaultSeed); }
		explicit FRandomStream(const int32 InSeed) { Initialize(InSeed); }

		void Initialize(int32 InSeed);
		// Restarts the sequence from the initial seed.
		void Reset();
		[[nodiscard]] int32 GetInitialSeed() const { return InitialSeed; }

		// Uniform in [0, 1) with 53 random bits: (next() >> 11) * 2^-53.
		[[nodiscard]] double GetFraction();
		// Uniform in [0, 1) with 24 random bits (never rounds up to 1.0f).
		[[nodiscard]] float FRand();
		[[nodiscard]] uint32 GetUnsignedInt();
		// Uniform integer in [0, A) for A > 0, otherwise 0.
		[[nodiscard]] int32 RandHelper(int32 A);
		// Uniform integer in [Min, Max].
		[[nodiscard]] int32 RandRange(int32 Min, int32 Max);
		// Uniform in [Min, Max).
		[[nodiscard]] double FRandRange(double Min, double Max);

	private:
		int32 InitialSeed = DefaultSeed;
		std::mt19937_64 Engine;
	};
}
