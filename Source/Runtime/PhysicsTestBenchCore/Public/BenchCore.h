// BenchCore.h - PT::BenchCore: measurement primitives below PT::Conformance and PT::Benchmark (02_Architecture 4.5, 9.3,
// A.12, DL53). [MVP M3: QPC clock buckets, window statistics, run ids, file hashes, read-only host facts and process
// memory counters; 확장 M7:
// FHostEnvironment controls (affinity, priority, timer resolution) and the process spawner; 확장 M10: FThreadCensus, TSC]
// Nothing here changes the machine: the power scheme is read and recorded, never set (01 Q9, 02 12.2).
#pragma once

#include "CoreTypes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace PhysicsTest
{
	// [MVP M3] QPC per bucket (FPlatformTime::Cycles64 is QPC; 02 9.3). [확장 M10] FCycleCounter (TSC) + QPC calibration.
	struct FClockBuckets
	{
		[[nodiscard]] static uint64 Begin();
		[[nodiscard]] static uint64 End(uint64 BeginTicks) { return Begin() - BeginTicks; }   // elapsed ticks since BeginTicks
		[[nodiscard]] static double TicksToNs(uint64 Ticks);
		[[nodiscard]] static uint64 GetTicksPerSecond();
	};

	// Statistics of one window of per-frame durations (ns).
	struct FDurationStats
	{
		uint32 Count = 0;
		double Mean = 0.0;
		double Median = 0.0;
		double P95 = 0.0;
		double P99 = 0.0;
		double Min = 0.0;
		double Max = 0.0;
	};

	// Nearest-rank percentiles on a sorted copy (P = ceil(p * n)-th smallest); Median is the mean of the two middle values
	// for an even count. An empty input gives Count 0 and zeros.
	[[nodiscard]] FDurationStats ComputeDurationStats(const std::vector<double>& Values);

	// A ULID (26 characters, Crockford base32: 48-bit Unix time in ms + 80 random bits): sortable run ids (02 12.3).
	[[nodiscard]] std::string MakeRunId();
	// UTC time as ISO 8601 ("2026-10-21T10:31:00Z").
	[[nodiscard]] std::string GetUtcTimestamp();
	// SHA-256 of a file's bytes as 64 hex characters; empty when the file cannot be read.
	[[nodiscard]] std::string HashFileSha256(const std::filesystem::path& Path);

	// Read-only host facts for the manifest (the controls arrive in M7).
	struct FHostFacts
	{
		std::string CpuBrand;                 // CPUID brand string
		uint32 LogicalProcessors = 0;
		std::string OsBuild;                  // "10.0.26200"
		std::string PowerSchemeName;          // the active power scheme's friendly name (read, never changed)
		std::string PowerSchemeGuid;
		uint64 QpcHz = 0;
		uint64 ProcessAffinityMask = 0;       // as the process started (not set in M3)
		std::string PriorityClass;            // as the process started (not set in M3)
	};
	[[nodiscard]] FHostFacts ReadHostFacts();

	// Process memory counters for the manifest's memory group (02 12.3): GetProcessMemoryInfo, read-only. Commit is the
	// process's private bytes (PROCESS_MEMORY_COUNTERS_EX::PrivateUsage / PeakPagefileUsage), WS the working set.
	struct FProcessMemory
	{
		bool bValid = false;
		uint64 CommitBytes = 0;
		uint64 PeakCommitBytes = 0;
		uint64 WorkingSetBytes = 0;
		uint64 PeakWorkingSetBytes = 0;
	};
	[[nodiscard]] FProcessMemory ReadProcessMemoryCounters();
}
