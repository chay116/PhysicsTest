// CycleCounter.cpp - FCycleCounter (HAL/PlatformTime.h): the TSC clock of the timing buckets and its QPC calibration (M10).
#include "HAL/PlatformTime.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace PhysicsTest
{
	namespace
	{
		std::once_flag GCalibrationOnce;
		std::atomic<double> GNsPerTick{ 0.0 };

		// The calibration spin: long enough that one QPC tick (100 ns on this machine) is a 5e-6 relative error.
		constexpr double CalibrationSeconds = 0.020;
	}

	FCycleCounter::FPair FCycleCounter::ReadPair()
	{
		FPair Best;
		uint64 BestSpread = ~0ull;
		for (int Attempt = 0; Attempt < 16; ++Attempt)
		{
			const uint64 Before = Read();
			const uint64 Qpc = FPlatformTime::Cycles64();
			const uint64 After = Read();
			const uint64 Spread = After - Before;
			if (Spread < BestSpread)
			{
				BestSpread = Spread;
				Best.Tsc = Before + Spread / 2;
				Best.Qpc = Qpc;
				Best.SpreadTicks = Spread;
			}
			// A pair within 4x of the tightest one seen is clean (no interrupt or context switch between the reads).
			if (Attempt >= 1 && Spread <= 4 * BestSpread)
			{
				break;
			}
		}
		return Best;
	}

	bool FCycleCounter::IsInvariant()
	{
		int Registers[4] = {};
		__cpuid(Registers, static_cast<int>(0x80000000u));
		if (static_cast<unsigned>(Registers[0]) < 0x80000007u)
		{
			return false;
		}
		__cpuid(Registers, static_cast<int>(0x80000007u));
		return (static_cast<unsigned>(Registers[3]) & (1u << 8)) != 0;
	}

	double FCycleCounter::CalibrateNsPerTick(const FPair& First, const FPair& Last)
	{
		if (Last.Tsc <= First.Tsc || Last.Qpc <= First.Qpc)
		{
			return 0.0;
		}
		const double Seconds = static_cast<double>(Last.Qpc - First.Qpc) * FPlatformTime::GetSecondsPerCycle64();
		return Seconds * 1.0e9 / static_cast<double>(Last.Tsc - First.Tsc);
	}

	void FCycleCounter::EnsureCalibrated()
	{
		std::call_once(GCalibrationOnce, []
		{
			const FPair First = ReadPair();
			const uint64 Deadline = First.Qpc + static_cast<uint64>(CalibrationSeconds * static_cast<double>(FPlatformTime::GetCyclesPerSecond64()));
			volatile uint64 Sink = 0;
			while (FPlatformTime::Cycles64() < Deadline)
			{
				Sink = Sink + 1;
			}
			const FPair Last = ReadPair();
			const double NsPerTick = CalibrateNsPerTick(First, Last);
			// A broken counter (not ordered) falls back to 1 ns per tick rather than 0 (every bucket would read 0 ns).
			GNsPerTick.store(NsPerTick > 0.0 ? NsPerTick : 1.0);
		});
	}

	double FCycleCounter::GetNsPerTick()
	{
		const double Value = GNsPerTick.load(std::memory_order_relaxed);
		if (Value > 0.0)
		{
			return Value;
		}
		EnsureCalibrated();
		return GNsPerTick.load();
	}

	double FCycleCounter::MeasureReadOverheadNs(const uint32 Reads)
	{
		const uint32 N = Reads > 0 ? Reads : 1;
		std::vector<double> Batches;
		Batches.reserve(32);
		volatile uint64 Sink = 0;
		const double NsPerTick = GetNsPerTick();
		for (int Batch = 0; Batch < 32; ++Batch)
		{
			const uint64 Begin = Read();
			for (uint32 Index = 0; Index < N; ++Index)
			{
				Sink = Sink + Read();
			}
			const uint64 End = Read();
			Batches.push_back(static_cast<double>(End - Begin) * NsPerTick / static_cast<double>(N));
		}
		std::sort(Batches.begin(), Batches.end());
		return 0.5 * (Batches[15] + Batches[16]);
	}
}
