// PlatformTime.h - FPlatformTime on QueryPerformanceCounter (UE: Windows/WindowsPlatformTime.h).
// MVP clock (DL10, S2): QPC. MSVC's std::chrono::steady_clock wraps the same counter (__msvc_chrono.hpp:649).
// The rdtscp + QPC-calibrated clock (FCycleCounter) arrives in M10.
#pragma once

#include "CoreTypes.h"

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
}
