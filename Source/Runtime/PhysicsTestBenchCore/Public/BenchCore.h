// BenchCore.h - PT::BenchCore: measurement primitives below PT::Conformance and PT::Benchmark (02_Architecture 4.5, 9.3,
// A.12, DL53). [MVP M3: QPC clock buckets, window statistics, run ids, file hashes, read-only host facts and process
// memory counters; 확장 M7: the processor topology, FHostEnvironment (per-process controls: affinity, priority, power
// throttling opt-out, timer resolution; the spin pre-roll; read-only CPU MHz and timer facts), FHostLoadMonitor (other
// processes' load on the run's cores, exit code 4), FProcessSpawner (fresh child processes for G8 / G11 and M8),
// FAllocationCounters (the bench exe's counting operator new) and the QPC read overhead; 확장 M10: FThreadCensus, TSC]
// Nothing here changes the machine: every control is per process (it ends with the process), and the power scheme is
// read and recorded, never set (01 Q9, 02 12.2).
#pragma once

#include "CoreTypes.h"

#include <atomic>
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

	// ---- [확장 M7] processor topology ---------------------------------------------------------------------------------
	// One physical core (GetLogicalProcessorInformationEx RelationProcessorCore, processor group 0): its logical CPUs and its
	// EfficiencyClass (Intel hybrid: P-cores 1, E-cores 0; a homogeneous CPU has 0 everywhere).
	struct FProcessorCore
	{
		uint64 LogicalMask = 0;        // bit i = logical CPU i
		uint32 FirstLogical = 0;
		uint8 EfficiencyClass = 0;
	};

	struct FProcessorTopology
	{
		std::vector<FProcessorCore> Cores;           // ascending FirstLogical
		uint8 MaxEfficiencyClass = 0;
		// The first logical CPU of every core of the highest efficiency class (the P-cores), ascending: one logical CPU per
		// P-core, so T threads never share a core's SMT pair (02 9.6; i5-13600KF: 0, 2, 4, 6, 8, 10).
		std::vector<uint32> PCoreFirstLogicals;
		uint64 SystemMask = 0;                       // every logical CPU of group 0

		// Every logical CPU of the cores that Mask touches (the SMT siblings included).
		[[nodiscard]] uint64 CoresOf(uint64 Mask) const;
		// The first T P-core logicals: 0x1, 0x5, 0x15, 0x55, 0x155, 0x555 on the i5-13600KF; 0 when T exceeds the P-cores.
		[[nodiscard]] uint64 MakeThreadMask(uint32 Threads) const;

		// How a mask sits on the cores (02 9.6: "HT(12)와 E코어 포함(20) 시리즈는 별도로 플래그를 단다").
		struct FMaskShape
		{
			uint32 Cores = 0;              // physical cores Mask touches
			uint32 SmtPairCores = 0;       // of those, cores with more than one of their logical CPUs in Mask (the HT series)
			uint32 ECores = 0;             // of those, cores below the highest efficiency class (the E-core series)
			const char* Series = "";       // "p_cores_one_lp" (one logical CPU per P-core, the default T series),
			                               // "p_cores_smt" (an SMT pair in the mask), "with_e_cores" (an E-core in the mask)
		};
		[[nodiscard]] FMaskShape DescribeMask(uint64 Mask) const;
	};
	[[nodiscard]] FProcessorTopology ReadProcessorTopology();
	[[nodiscard]] uint32 CountBits(uint64 Mask);
	[[nodiscard]] std::vector<uint32> MaskToCpus(uint64 Mask);

	// ---- [확장 M7] FHostEnvironment: per-process host controls, applied before any SDK init or LoadLibrary (02 12.2) ----
	struct FTimerResolution
	{
		bool bValid = false;
		double CoarsestMs = 0.0;       // NtQueryTimerResolution "MinimumResolution" (e.g. 15.625 ms)
		double FinestMs = 0.0;         // "MaximumResolution" (e.g. 0.5 ms)
		double CurrentMs = 0.0;        // the system's current period
	};

	struct FCpuFrequency
	{
		uint32 Cpu = 0;
		uint32 MaxMhz = 0;
		uint32 CurrentMhz = 0;         // CallNtPowerInformation(ProcessorInformation); hybrid Intel parts often report the
		uint32 MhzLimit = 0;           // nominal value here (recorded as read, not interpreted)
	};

	class FHostEnvironment
	{
	public:
		// SetProcessAffinityMask: threads created later (Jolt workers, PhysX dispatcher threads) inherit it.
		static bool ApplyAffinity(uint64 Mask, std::string& OutError);
		static bool SetHighPriority(std::string& OutError);              // HIGH_PRIORITY_CLASS
		// SetProcessInformation(ProcessPowerThrottling): EXECUTION_SPEED and IGNORE_TIMER_RESOLUTION controlled and off
		// (EcoQoS opt-out). Per process; no system setting changes.
		static bool DisablePowerThrottling(std::string& OutError);
		// timeBeginPeriod(1) for this process, as a UE DLL does process-wide (WindowsPlatformMisc.cpp:1076 [V]) - applied to
		// every engine alike. Ends with the process.
		static bool SetTimerResolution1ms(std::string& OutError);
		[[nodiscard]] static FTimerResolution QueryTimerResolution();
		[[nodiscard]] static std::vector<FCpuFrequency> ReadCpuFrequencies(uint64 Mask);
		[[nodiscard]] static std::string GetPriorityClassName();
		// Spins one thread on every CPU of Mask for Seconds (the calling thread on the lowest one, temporarily pinned): the
		// cores leave their idle states before frame 0 (02 9.5, 03 M7). Helper threads are created and joined here.
		static void SpinPreRoll(double Seconds, uint64 Mask);
		// The exit-4 test helper (M7): spins every CPU of Mask at HIGH_PRIORITY_CLASS for Seconds, then returns.
		static void RunBusyLoad(double Seconds, uint64 Mask);
	};

	// ---- [확장 M7] FHostLoadMonitor: other processes' CPU time on the run's cores -----------------------------------------
	// Cycle-exact (TSC units): per-CPU busy cycles = elapsed TSC - QueryIdleProcessorCycleTime, this process's cycles from
	// QueryProcessCycleTime. Interrupt and DPC time on a CPU counts as "other" (it is not this process's thread time).
	// The run's cores are the physical cores its mask touches, SMT siblings included (FProcessorTopology::CoresOf): a load on
	// a sibling logical CPU shares the core's execution units with the run, so it counts (docs/TimingContract.md 5.1).
	struct FHostLoad
	{
		bool bValid = false;
		double WallSeconds = 0.0;
		double OwnProcessCorePct = 0.0;          // this process, in % of one core
		double CoreBusyOtherCorePct = 0.0;       // on every logical CPU of the run's cores (mask + SMT siblings), minus this
		                                         // process: the exit-4 measure (compared with the per-core limit x Cores)
		double MaskBusyOtherCorePct = 0.0;       // on the mask's own CPUs, minus this process: recorded
		double SiblingBusyCorePct = 0.0;         // on the SMT siblings of those CPUs (outside the mask): recorded
		double SystemBusyOtherCorePct = 0.0;     // every CPU minus this process: recorded
		std::vector<double> PerCpuBusyPct;       // per logical CPU of group 0, in % of that CPU
	};

	class FHostLoadMonitor
	{
	public:
		void Begin();
		// Mask: the run's affinity mask; CoreMask: every logical CPU of its cores (FProcessorTopology::CoresOf(Mask)).
		[[nodiscard]] FHostLoad End(uint64 Mask, uint64 CoreMask) const;

	private:
		uint64 TscBegin = 0;
		uint64 QpcBegin = 0;
		uint64 ProcessCyclesBegin = 0;
		std::vector<uint64> IdleCyclesBegin;
		bool bStarted = false;
	};

	// ---- [확장 M7] FProcessSpawner: one fresh child process per run (G8 DET1, G11; the M8 runner does the same) ----------
	struct FChildResult
	{
		bool bStarted = false;
		bool bTimedOut = false;
		int32 ExitCode = -1;
		double WallSeconds = 0.0;
		std::string Error;
	};

	class FProcessSpawner
	{
	public:
		// CreateProcessW(CommandLine) with stdout and stderr redirected to LogPath (truncated), inside a job object that kills
		// the child when this process dies; waits up to TimeoutSeconds, then terminates the job (bTimedOut).
		[[nodiscard]] static FChildResult RunChild(const std::wstring& CommandLine, double TimeoutSeconds, const std::filesystem::path& LogPath);
	};

	// ---- [확장 M7] allocation counting hook ------------------------------------------------------------------------------
	// PhysicsTestBench.exe replaces the global operator new / delete (AllocationHook.cpp) and counts every call here (all
	// C++ allocations of the image: harness, Kit, adapters and the SDK code that uses operator new). The bench reads the
	// counters around each Tick: the timed loop must not allocate (02 9.3, 03 M7).
	struct FAllocationCounters
	{
		static std::atomic<uint64> NumNew;
		static std::atomic<uint64> NumDelete;
		static std::atomic<bool> bHookInstalled;   // set by the replaced operator new's translation unit at start-up
	};

	// ---- [확장 M7] clock facts -------------------------------------------------------------------------------------------
	// The cost of one FClockBuckets::Begin() (QueryPerformanceCounter): the median of 32 batches of Reads calls, in ns.
	[[nodiscard]] double MeasureClockReadOverheadNs(uint32 Reads = 20000);
}
