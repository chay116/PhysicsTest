// BenchCore.h - PT::BenchCore: measurement primitives below PT::Conformance and PT::Benchmark (02_Architecture 4.5, 9.3,
// A.12, DL53). [MVP M3: QPC clock buckets, window statistics, run ids, file hashes, read-only host facts and process
// memory counters; 확장 M7: the processor topology, FHostEnvironment (per-process controls: affinity, priority, power
// throttling opt-out, timer resolution; the spin pre-roll; read-only CPU MHz and timer facts), FHostLoadMonitor (other
// processes' load on the run's cores, exit code 4), FProcessSpawner (fresh child processes for G8 / G11 and M8),
// FAllocationCounters (the bench exe's counting operator new) and the QPC read overhead; 확장 M10: FThreadCensus and the
// census window (cpu_eff, foreign thread cycles: G10b), the run's TSC calibration (FCycleCounter is in Core)]
// Nothing here changes the machine: every control is per process (it ends with the process), and the power scheme is
// read and recorded, never set (01 Q9, 02 12.2).
#pragma once

#include "CoreTypes.h"
#include "HAL/PlatformTime.h"

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

	// ---- [확장 M10] the run's TSC calibration (02 9.3, docs/TimingContract.md 2.1) ----------------------------------------
	// TSC / QPC pairs taken over one run (host controls .. last frame). NsPerTick is the two-point calibration between the
	// first and the last pair; the residual is the largest deviation of any pair from that line, relative to the span
	// (|((Q_i - Q_0) / (Q_n - Q_0)) - ((T_i - T_0) / (T_n - T_0))|), and is what 03 M10 bounds by 1e-5. bValid only says the
	// line exists (two ordered pairs); M10 fix round 1: a run whose residual is not below TscCalibrationResidualMax is not
	// headline-valid (docs/TimingContract.md 6 #12).
	inline constexpr double TscCalibrationResidualMax = 1.0e-5;
	struct FTscCalibration
	{
		bool bValid = false;
		double NsPerTick = 0.0;
		double TscHz = 0.0;
		double SpanSeconds = 0.0;
		uint32 Pairs = 0;
		double MaxResidual = 0.0;          // relative to the span
		double RmsResidual = 0.0;
		uint64 MaxPairSpreadTicks = 0;     // the widest kept pair (FCycleCounter::ReadPair)
	};
	[[nodiscard]] FTscCalibration CalibrateTsc(const std::vector<FCycleCounter::FPair>& Pairs);

	// ---- [확장 M10] FThreadCensus: the threads of this process and their CPU cycles (02 12.2 steps 5 / 11, 14.3 G10b) ------
	// QueryThreadCycleTime (user + kernel, TSC units) of every thread of this process (Toolhelp32 snapshot), the process
	// total (QueryProcessCycleTime: every thread, exited ones included), every logical CPU's idle cycles
	// (QueryIdleProcessorCycleTime) and the TSC / QPC at the capture. A census keeps a handle to every thread it saw, so a
	// thread that exits before the window ends still reports its cycles (the thread object lives while a handle is open).
	// Nothing here sets anything: it reads this process's own threads.
	// M10 fix round 0: every counter of a window bound is read together, next to the main thread's own counter and outside the
	// (slow: tens of ms) Toolhelp32 enumeration - the process total, the cycles of the threads known at that moment (census #1:
	// its own threads, through the handles its enumeration opened; census #2: census #1's threads, through census #1's handles),
	// the idle cycles, the main thread, the TSC and QPC. Only a thread born inside the window is read by census #2's
	// enumeration, a few ms after the window ends.
	struct FCensusThread
	{
		uint32 ThreadId = 0;
		std::string Name;               // GetThreadDescription (Windows 10 1607+); "" when the thread has none
		uint64 Cycles = 0;              // since the thread started
		bool bValid = false;            // QueryThreadCycleTime succeeded
		bool bMain = false;             // the thread that took the census (the bench's timing thread)
	};

	class FThreadCensus
	{
	public:
		// Where the cycle counters are read relative to the (slow, a few ms) thread enumeration: a window's Begin reads them
		// last, its End first, so the enumeration stays outside the window.
		enum class ETiming : uint8
		{
			CountersLast,
			CountersFirst,
		};

		FThreadCensus() = default;
		~FThreadCensus();
		FThreadCensus(const FThreadCensus&) = delete;
		FThreadCensus& operator=(const FThreadCensus&) = delete;
		FThreadCensus(FThreadCensus&& Other) noexcept;
		FThreadCensus& operator=(FThreadCensus&& Other) noexcept;

		// Every thread of this process; false (and Error) when the snapshot fails. WindowBegin (census #2 of a window): census
		// #1, whose threads are read through its held handles together with this census's counters (WindowBeginReads).
		bool Capture(ETiming Timing = ETiming::CountersLast, const FThreadCensus* WindowBegin = nullptr);
		[[nodiscard]] bool IsValid() const { return bValid; }
		[[nodiscard]] const std::vector<FCensusThread>& GetThreads() const { return Threads; }
		[[nodiscard]] uint32 GetMainThreadId() const { return MainThreadId; }
		[[nodiscard]] uint64 GetMainThreadCycles() const { return MainThreadCycles; }
		[[nodiscard]] uint64 GetProcessCycles() const { return ProcessCycles; }
		// Census #1 (CountersLast): the process cycles that the window's bracket of reads leaves unexplained with nothing inside
		// it (the median of three null brackets); 0 for census #2.
		[[nodiscard]] uint64 GetBracketCycles() const { return BracketCycles; }
		[[nodiscard]] uint64 GetTsc() const { return Tsc; }
		[[nodiscard]] uint64 GetQpc() const { return Qpc; }
		[[nodiscard]] const std::vector<uint64>& GetIdleCycles() const { return IdleCycles; }
		[[nodiscard]] const std::string& GetError() const { return Error; }
		// The current cycles of a thread this census holds a handle to (an exited thread keeps its last value); false when the
		// census does not know the thread.
		bool ReadHeldThreadCycles(uint32 ThreadId, uint64& OutCycles) const;
		// Census #2 of a window: census #1's threads read at this census's counters (alive or exited; bValid false when the
		// read failed). Empty when Capture had no WindowBegin.
		[[nodiscard]] const std::vector<FCensusThread>& GetWindowBeginReads() const { return WindowBeginReads; }

	private:
		void CloseHandles();
		void ReadHeldHandles();
		[[nodiscard]] uint64 SumHeldCycles() const;
		[[nodiscard]] uint64 MeasureBracketCycles() const;

		std::vector<FCensusThread> Threads;
		std::vector<FCensusThread> WindowBeginReads;
		std::vector<void*> Handles;     // parallel to Threads (HANDLE), nullptr when OpenThread failed
		uint32 MainThreadId = 0;
		uint64 MainThreadCycles = 0;
		uint64 ProcessCycles = 0;
		uint64 BracketCycles = 0;
		uint64 Tsc = 0;
		uint64 Qpc = 0;
		std::vector<uint64> IdleCycles; // per logical CPU of group 0
		std::string Error;
		bool bValid = false;
	};

	// One census window (Begin .. End, both taken by the timing thread): who did the process's work, and how much of the run's
	// cores it had (03 M10 G10b: at T = 1 cpu_eff in [0.98, 1.02] and foreign thread cycles < 0.5 %).
	//   own        main + foreign: the cycles of this process's threads over the window (per thread: census #1's threads read at
	//              both bounds' counters, a thread born inside the window from census #2's enumeration)
	//   foreign    every thread but the main (timing) thread: engine workers, a UE TaskGraph, the loader's thread pool
	//   unattributed  process - own: a thread born and gone inside the window (in neither census) - G10b counts it as foreign
	//   cpu_eff    own / (wall x T - other) where other = the busy cycles of the mask's CPUs that are not this process's
	//              (other processes, interrupts and DPCs). The design's raw value own / (wall x T) is kept as cpu_eff_raw:
	//              on a desktop in use it moves with the foreign load that exit code 4 already bounds; cpu_eff answers "did
	//              the process keep the cycles it was given busy" (an engine that waits on its workers, or sleeps, drops it)
	struct FCensusThreadDelta
	{
		uint32 ThreadId = 0;
		std::string Name;
		uint64 Cycles = 0;              // over the window
		bool bMain = false;
		bool bNew = false;              // not in the Begin census (its cycles since it started count)
		bool bExited = false;           // in Begin, gone at End (its cycles up to its exit count)
	};

	struct FCensusWindow
	{
		bool bValid = false;
		std::string Error;
		uint32 Threads = 1;             // T
		uint64 Mask = 0;
		double WallSeconds = 0.0;       // QPC
		uint64 WallTsc = 0;
		uint64 MainCycles = 0;
		uint64 ForeignCycles = 0;
		uint64 OwnCycles = 0;           // main + foreign
		uint64 ProcessCycles = 0;       // QueryProcessCycleTime over the window, read with the bounds' counters (first at census #1,
		                                // last at census #2): its window encloses main's and the census #1 threads' by microseconds
		uint64 UnattributedCycles = 0;  // max(0, ProcessCycles - own): a thread born and gone inside the window (in no census), plus
		                                // what the bracket's reads themselves leave (BracketCycles, ~0.1 ms); clamped at 0 because a
		                                // thread born inside the window is read by census #2's enumeration, after the process total
		uint64 BracketCycles = 0;       // census #1's null-bracket baseline (M10 fix round 0)
		uint64 MaskBusyCycles = 0;      // the mask's CPUs: elapsed - idle
		uint64 OtherOnMaskCycles = 0;   // MaskBusy - own (>= 0)
		double CpuEff = 0.0;
		double CpuEffRaw = 0.0;
		double ForeignFraction = 0.0;   // foreign / own
		double UnattributedFraction = 0.0;   // max(0, unattributed - bracket) / own (G10b judges foreign + this)
		uint32 ThreadsBegin = 0;
		uint32 ThreadsEnd = 0;
		std::vector<FCensusThreadDelta> PerThread;   // every thread seen in either census, main first, then by cycles

		// M10 fix round 1: the window in this process's own CPU time (own cycles at the window's TSC rate). On a starved core it is
		// far below WallSeconds; G10b's minimum window counts this, since its 0.5 % bound is a share of own.
		[[nodiscard]] double GetOwnSeconds() const
		{
			return WallTsc > 0 ? static_cast<double>(OwnCycles) * WallSeconds / static_cast<double>(WallTsc) : 0.0;
		}
		// The share of the mask's capacity (wall x T) that other processes, interrupts and DPCs took: G10b's contended rule.
		[[nodiscard]] double GetOtherOnMaskFraction() const
		{
			const double Capacity = static_cast<double>(WallTsc) * static_cast<double>(Threads > 0 ? Threads : 1u);
			return Capacity > 0.0 ? static_cast<double>(OtherOnMaskCycles) / Capacity : 0.0;
		}
	};
	[[nodiscard]] FCensusWindow MeasureCensusWindow(const FThreadCensus& Begin, const FThreadCensus& End, uint64 Mask, uint32 Threads);
}
