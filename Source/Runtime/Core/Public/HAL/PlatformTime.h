// PlatformTime.h - FPlatformTime on QueryPerformanceCounter (UE: Windows/WindowsPlatformTime.h) and, since M10, FCycleCounter
// (rdtscp + lfence, calibrated against QPC; A.5, 02_Architecture 9.3).
// MVP clock (DL10, S2): QPC. MSVC's std::chrono::steady_clock wraps the same counter (__msvc_chrono.hpp:649).
#pragma once

#include "CoreTypes.h"

#include <intrin.h>   // __rdtscp, _mm_lfence

namespace PhysicsTest
{
	struct FPlatformTime
	{
		// Raw QPC ticks.
		[[nodiscard]] static uint64 Cycles64();
		// QPC ticks per second (qpc_hz in run manifests).
		[[nodiscard]] static uint64 GetCyclesPerSecond64();
		[[nodiscard]] static double GetSecondsPerCycle64();
		// Seconds since an arbitrary epoch (QPC based).
		[[nodiscard]] static double Seconds();
		[[nodiscard]] static double ToSeconds64(const uint64 Cycles) { return double(Cycles) * GetSecondsPerCycle64(); }
	};

	// [확장 M10] The clock of the timing buckets (02 9.3, A.5; 03 M10 task 1): the time-stamp counter.
	//   Read      rdtscp (it waits until every earlier instruction has executed) followed by lfence (no later instruction
	//             starts before the read completes), so two reads bracket exactly the instructions between them. The TSC
	//             of this CPU is invariant (CPUID 0x80000007 EDX bit 8: constant rate across P-states and C-states, the same
	//             counter on every core), which IsInvariant reports and the manifest records.
	//   ns        the tick length comes from QPC. The process-wide value (EnsureCalibrated: one ~20 ms spin against QPC, the
	//             first time it is needed, never inside a bucket) converts the buckets of the viewer and the tests; the bench
	//             calibrates over each whole run (TSC / QPC pairs from its host controls to the last frame) and converts the
	//             recorded tick counts again (docs/TimingContract.md 2.1).
	// Name note (M10 fix round 1): the name comes from the design (A.5). UE has an unrelated class of the same name, the stats
	// system's scope-cycle event emitter behind SCOPE_CYCLE_COUNTER (Engine/Source/Runtime/Core/Public/Stats/StatsSystemTypes.h:1496);
	// this one is only the TSC clock and never emits stats.
	struct FCycleCounter
	{
		// One ordered TSC read (rdtscp + lfence).
		[[nodiscard]] static FORCEINLINE uint64 Read()
		{
			unsigned int Aux = 0;
			const uint64 Tsc = __rdtscp(&Aux);
			_mm_lfence();
			return Tsc;
		}

		// A TSC / QPC pair taken back to back: the TSC is the midpoint of two reads around one QPC read. When the two TSC
		// reads lie further apart than 4x the fastest pair seen in this call (an interrupt or a context switch between them),
		// the pair is read again (up to 16 attempts; the tightest is kept). SpreadTicks: the TSC distance of the kept pair.
		struct FPair
		{
			uint64 Tsc = 0;
			uint64 Qpc = 0;
			uint64 SpreadTicks = 0;
		};
		[[nodiscard]] static FPair ReadPair();

		// CPUID 0x80000007 EDX bit 8 (invariant TSC).
		[[nodiscard]] static bool IsInvariant();
		// The process-wide calibration (once; thread-safe): ~20 ms of spinning between two pairs.
		static void EnsureCalibrated();
		// ns per TSC tick of the process-wide calibration (calibrates on first use).
		[[nodiscard]] static double GetNsPerTick();
		[[nodiscard]] static double TicksToNs(const uint64 Ticks) { return static_cast<double>(Ticks) * GetNsPerTick(); }
		// Two-point calibration: ns per TSC tick between two pairs (0 when they are not ordered).
		[[nodiscard]] static double CalibrateNsPerTick(const FPair& First, const FPair& Last);
		// The cost of one Read(): the median of 32 batches of Reads calls, converted with the process-wide calibration, in ns.
		[[nodiscard]] static double MeasureReadOverheadNs(uint32 Reads = 20000);
	};
}
