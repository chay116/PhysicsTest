// MeasurementTests.cpp - unit tests of the M10 measurement pieces (03_WorkPlan M10 tasks 1, 2, 4; 02_Architecture 9.3, 12.4, 14.3
// G10b): the TSC clock (FCycleCounter) and the run calibration's residual, the thread census window (own / main / foreign cycles,
// cpu_eff, threads born and gone inside the window), the G10b verdicts, and the conformance stamp key's canonical form. The
// process-level behaviour (a run's census and G10b, -Conformance's stamp, the runner's refusal) is covered by the
// PhysicsTest.Bench.* CTests and PhysicsTest.Py.Integration.
#include "CoreMinimal.h"

#include "BenchCore.h"
#include "ConformanceStamp.h"
#include "Gates/ThreadGate.h"
#include "HAL/PlatformTime.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace PhysicsTest;

namespace
{
	// Spins the calling thread for Seconds (QPC deadline): a busy core, no sleep.
	void SpinFor(const double Seconds)
	{
		const uint64 Deadline = FPlatformTime::Cycles64() + static_cast<uint64>(Seconds * static_cast<double>(FPlatformTime::GetCyclesPerSecond64()));
		volatile uint64 Sink = 0;
		while (FPlatformTime::Cycles64() < Deadline)
		{
			Sink = Sink + 1;
		}
	}

	// Spins the calling thread until it has used Cycles of its own (QueryThreadCycleTime): the same work whatever the load.
	void SpinCycles(const uint64 Cycles)
	{
		ULONG64 Begin = 0;
		QueryThreadCycleTime(GetCurrentThread(), &Begin);
		ULONG64 Now = Begin;
		volatile uint64 Sink = 0;
		while (Now - Begin < Cycles)
		{
			for (int32 Index = 0; Index < 1000; ++Index)
			{
				Sink = Sink + 1;
			}
			QueryThreadCycleTime(GetCurrentThread(), &Now);
		}
	}

	// A synthetic window: own = 1 M cycles over Seconds of own CPU time; Other = the share of the mask's capacity other processes
	// took (the wall grows with it: own + other = the mask's busy cycles, the process had what the others left).
	FCensusWindow MakeWindow(const uint32 Threads, const double CpuEff, const double Foreign, const double Seconds = 0.5,
		const double Unattributed = 0.0, const double Other = 0.0)
	{
		FCensusWindow Window;
		Window.bValid = true;
		Window.Threads = Threads;
		Window.OwnCycles = 1000000;
		Window.WallTsc = static_cast<uint64>(1.0e6 / (1.0 - Other));
		Window.WallSeconds = Seconds / (1.0 - Other);
		Window.MaskBusyCycles = Window.WallTsc;
		Window.OtherOnMaskCycles = Window.WallTsc - Window.OwnCycles;
		Window.ForeignCycles = static_cast<uint64>(Foreign * 1.0e6);
		Window.MainCycles = Window.OwnCycles - Window.ForeignCycles;
		Window.CpuEff = CpuEff;
		Window.CpuEffRaw = CpuEff;
		Window.ForeignFraction = Foreign;
		Window.UnattributedCycles = static_cast<uint64>(Unattributed * 1.0e6);
		Window.UnattributedFraction = Unattributed;
		Window.ProcessCycles = Window.OwnCycles + Window.UnattributedCycles;
		Window.PerThread.push_back(FCensusThreadDelta{ 1, "", Window.MainCycles, true, false, false });
		if (Window.ForeignCycles > 0)
		{
			Window.PerThread.push_back(FCensusThreadDelta{ 2, "Worker", Window.ForeignCycles, false, false, false });
		}
		return Window;
	}
}

TEST_CASE("PhysicsTest.Measurement.CycleCounter")
{
	// 02 9.3: rdtscp + lfence reads are ordered and monotonic on one thread; the TSC of this CPU is invariant (recorded in every
	// manifest); the process-wide calibration and the read overhead are in a plausible range.
	const uint64 A = FCycleCounter::Read();
	const uint64 B = FCycleCounter::Read();
	CHECK(B >= A);
	MESSAGE("invariant TSC: " << (FCycleCounter::IsInvariant() ? "yes" : "no"));
	const double NsPerTick = FCycleCounter::GetNsPerTick();
	CHECK(NsPerTick > 0.05);    // < 20 GHz
	CHECK(NsPerTick < 10.0);    // > 100 MHz
	const FCycleCounter::FPair Pair = FCycleCounter::ReadPair();
	CHECK(Pair.Tsc > 0);
	CHECK(Pair.Qpc > 0);
	CHECK(Pair.SpreadTicks < 100000);   // the kept pair is tight (well under 100 us)
	const double Overhead = FCycleCounter::MeasureReadOverheadNs(2000);
	CHECK(Overhead > 0.0);
	CHECK(Overhead < 1000.0);
	MESSAGE("TSC: " << 1.0 / NsPerTick << " GHz, read overhead " << Overhead << " ns");
	// Ticks convert linearly.
	CHECK(FCycleCounter::TicksToNs(1000) == doctest::Approx(1000.0 * NsPerTick));
}

TEST_CASE("PhysicsTest.Measurement.TscCalibrationResidual")
{
	// The run calibration (02 9.3, 03 M10 "TSC-QPC 보정 잔차 < 1e-5"): two-point ns per tick, the residual of every pair from that
	// line relative to the span.
	std::vector<FCycleCounter::FPair> Linear = { { 1000, 100, 10 }, { 3000, 300, 10 }, { 5000, 500, 10 }, { 11000, 1100, 12 } };
	const FTscCalibration Exact = CalibrateTsc(Linear);
	REQUIRE(Exact.bValid);
	CHECK(Exact.MaxResidual == doctest::Approx(0.0));
	CHECK(Exact.NsPerTick == doctest::Approx(0.1 * 1.0e9 * FPlatformTime::GetSecondsPerCycle64()));
	CHECK(Exact.MaxPairSpreadTicks == 12);
	// A pair 1 % of the span off the line: residual 0.01.
	std::vector<FCycleCounter::FPair> Bent = Linear;
	Bent[1].Qpc += 10;   // (310 - 100) / 1000 vs (3000 - 1000) / 10000
	CHECK(CalibrateTsc(Bent).MaxResidual == doctest::Approx(0.01));
	CHECK_FALSE(CalibrateTsc({ { 5, 5, 0 } }).bValid);
	CHECK_FALSE(CalibrateTsc({ { 5, 5, 0 }, { 5, 9, 0 } }).bValid);   // no TSC span
	// The real clocks over a 60 ms spin: the residual of six pairs stays far below 1e-5 (the QPC tick is 100 ns).
	std::vector<FCycleCounter::FPair> Real;
	for (int Index = 0; Index < 6; ++Index)
	{
		Real.push_back(FCycleCounter::ReadPair());
		SpinFor(0.012);
	}
	const FTscCalibration Measured = CalibrateTsc(Real);
	REQUIRE(Measured.bValid);
	CHECK(Measured.MaxResidual < 1.0e-5);
	CHECK(Measured.NsPerTick == doctest::Approx(FCycleCounter::GetNsPerTick()).epsilon(1.0e-3));
	MESSAGE("6 pairs over " << Measured.SpanSeconds * 1.0e3 << " ms: residual " << Measured.MaxResidual << ", " << Measured.TscHz * 1.0e-9 << " GHz");
}

TEST_CASE("PhysicsTest.Measurement.ThreadCensus")
{
	// 02 12.2 steps 5 / 11: the census sees this process's threads; a window measures who worked. A worker born inside the
	// window counts with all its cycles (bNew), one that exits inside it with its cycles up to the exit (bExited, through the
	// handle census #1 holds, read at census #2's counters). M10 fix round 0: a worker born AND gone inside the window is in
	// neither census; the process total (read with the bounds' counters) shows it as unattributed.
	std::atomic<bool> bRelease{ false };
	std::thread Exiting([&bRelease]
	{
		while (!bRelease.load())
		{
			std::this_thread::yield();
		}
		SpinFor(0.020);
	});
	SpinFor(0.002);
	FThreadCensus Begin;
	REQUIRE(Begin.Capture(FThreadCensus::ETiming::CountersLast));
	CHECK(Begin.GetThreads().size() >= 2);
	bRelease.store(true);
	std::atomic<bool> bBornDone{ false };
	std::thread Born([&bBornDone]
	{
		SpinFor(0.020);
		while (!bBornDone.load())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	});
	const uint64 EphemeralCycles = static_cast<uint64>(0.010 / (FCycleCounter::GetNsPerTick() * 1.0e-9));   // ~10 ms of one core
	std::thread Ephemeral([EphemeralCycles] { SpinCycles(EphemeralCycles); });
	SpinFor(0.030);
	Exiting.join();
	Ephemeral.join();
	FThreadCensus End;
	REQUIRE(End.Capture(FThreadCensus::ETiming::CountersFirst, &Begin));
	CHECK(End.GetWindowBeginReads().size() == Begin.GetThreads().size());
	bBornDone.store(true);
	Born.join();
	const FCensusWindow Window = MeasureCensusWindow(Begin, End, ~0ull, 1);
	REQUIRE(Window.bValid);
	CHECK(Window.MainCycles > 0);
	CHECK(Window.ForeignCycles > 0);
	CHECK(Window.OwnCycles == Window.MainCycles + Window.ForeignCycles);
	CHECK(Window.ForeignFraction > 0.1);   // two 20 ms workers next to a 30 ms main thread
	bool bSawNew = false;
	bool bSawExited = false;
	for (const FCensusThreadDelta& Thread : Window.PerThread)
	{
		bSawNew = bSawNew || (Thread.bNew && Thread.Cycles > 0);
		bSawExited = bSawExited || (Thread.bExited && Thread.Cycles > 0);
	}
	CHECK(bSawNew);
	CHECK(bSawExited);
	// The process total's window encloses census #1's threads' and main's (all read with the bounds' counters); the thread born
	// and gone inside the window is in no census and only in that total (unattributed), with at least the cycles it spun.
	CHECK(Window.UnattributedCycles == (Window.ProcessCycles > Window.OwnCycles ? Window.ProcessCycles - Window.OwnCycles : 0));
	CHECK(static_cast<double>(Window.UnattributedCycles) >= 0.9 * static_cast<double>(EphemeralCycles));
	CHECK(Window.UnattributedFraction > 0.0);
	MESSAGE("window: own " << Window.OwnCycles << ", process " << Window.ProcessCycles << ", unattributed " << Window.UnattributedCycles
		<< " (the ephemeral thread spun " << EphemeralCycles << ")");
	CHECK(Window.PerThread.front().bMain);
	// The main thread alone: the other threads of this process (doctest has none of its own) do next to nothing. M10 fix round 1:
	// 60 ms of this thread's own cycles, not of wall time (G10b's minimum is own CPU time; under a parallel CTest the core may be
	// shared).
	FThreadCensus Quiet0;
	REQUIRE(Quiet0.Capture(FThreadCensus::ETiming::CountersLast));
	SpinCycles(static_cast<uint64>(0.060 / (FCycleCounter::GetNsPerTick() * 1.0e-9)));
	FThreadCensus Quiet1;
	REQUIRE(Quiet1.Capture(FThreadCensus::ETiming::CountersFirst, &Quiet0));
	const FCensusWindow Quiet = MeasureCensusWindow(Quiet0, Quiet1, ~0ull, 1);
	REQUIRE(Quiet.bValid);
	// Every other thread is idle (the prof preset's Tracy client has one thread, "Tracy Profiler", which waits for a server:
	// TRACY_NO_CALLSTACK leaves out the symbol worker that used to work right after start, M10 fix round 0), and the counter
	// reads around the window leave next to nothing unattributed: a 50 ms window of one thread passes G10b's 0.5 %.
	CHECK(Quiet.GetOwnSeconds() >= Gates::G10bMinOwnSeconds);
	CHECK(Quiet.ForeignFraction + Quiet.UnattributedFraction < 0.005);
	MESSAGE("quiet window: foreign " << Quiet.ForeignFraction * 100.0 << " %, unattributed " << Quiet.UnattributedFraction * 100.0 << " % ("
		<< Quiet.UnattributedCycles << " cycles), cpu_eff_raw (all CPUs) " << Quiet.CpuEffRaw);
}

TEST_CASE("PhysicsTest.Measurement.G10bVerdicts")
{
	// 03 M10 / 02 14.3 G10b: at T = 1 cpu_eff in [0.98, 1.02] and foreign thread cycles < 0.5 %; T > 1 and a too short window
	// report Skip with the values; no census window is a Skip with the reason.
	CHECK(Gates::EvaluateG10b(MakeWindow(1, 1.0, 0.0), "t").Result == EGateResult::Pass);
	CHECK(Gates::EvaluateG10b(MakeWindow(1, 0.985, 0.004), "t").Result == EGateResult::Pass);
	const FGateReport Foreign = Gates::EvaluateG10b(MakeWindow(1, 1.0, 0.01), "t");
	CHECK(Foreign.Result == EGateResult::Fail);
	REQUIRE(Foreign.Failures.size() == 1);
	CHECK(Foreign.Failures.front().find("foreign thread cycles") != std::string::npos);
	CHECK(Gates::EvaluateG10b(MakeWindow(1, 0.95, 0.0), "t").Result == EGateResult::Fail);
	CHECK(Gates::EvaluateG10b(MakeWindow(1, 1.05, 0.0), "t").Result == EGateResult::Fail);
	Gates::EG10bSkip Why = Gates::EG10bSkip::None;
	CHECK(Gates::EvaluateG10b(MakeWindow(4, 0.7, 0.6), "t", &Why).Result == EGateResult::Skip);
	CHECK(Why == Gates::EG10bSkip::MultiThread);
	CHECK(Gates::EvaluateG10b(MakeWindow(1, 1.0, 0.0, 0.001), "t", &Why).Result == EGateResult::Skip);
	CHECK(Why == Gates::EG10bSkip::ShortWindow);
	// M10 fix round 1: the minimum is own CPU time. 30 ms of own cycles in a 100 ms window (others took 70 % of the core) is
	// too short, whatever the wall time; 60 ms of own in a 300 ms window is long enough.
	CHECK(Gates::EvaluateG10b(MakeWindow(1, 1.0, 0.0, 0.030, 0.0, 0.7), "t", &Why).Result == EGateResult::Skip);
	CHECK(Why == Gates::EG10bSkip::ShortWindow);
	CHECK(Gates::EvaluateG10b(MakeWindow(1, 1.0, 0.0, 0.060, 0.0, 0.8), "t", &Why).Result == EGateResult::Pass);
	CHECK(Why == Gates::EG10bSkip::None);
	// A contended window (others took 75 % of the core): 0.8 % of own is 0.2 % of the mask's busy cycles - what a thread that runs
	// in proportion to wall time (0.2 % of wall) shows on a core the process had a quarter of - so no verdict (Skip contended);
	// 4 % of own is 1 % of the busy cycles - real work even after the starvation - so Fail. Uncontended, 0.8 % fails.
	CHECK(Gates::EvaluateG10b(MakeWindow(1, 1.0, 0.008, 0.5, 0.0, 0.75), "t", &Why).Result == EGateResult::Skip);
	CHECK(Why == Gates::EG10bSkip::Contended);
	CHECK(Gates::EvaluateG10b(MakeWindow(1, 1.0, 0.0, 0.5, 0.008, 0.75), "t", &Why).Result == EGateResult::Skip);
	const FGateReport Starved = Gates::EvaluateG10b(MakeWindow(1, 1.0, 0.03, 0.5, 0.01, 0.75), "t", &Why);
	CHECK(Starved.Result == EGateResult::Fail);
	CHECK(Why == Gates::EG10bSkip::None);
	REQUIRE(Starved.Failures.size() == 1);
	CHECK(Starved.Failures.front().find("contended window") != std::string::npos);
	CHECK(Gates::EvaluateG10b(MakeWindow(1, 1.0, 0.008, 0.5, 0.0, 0.04), "t").Result == EGateResult::Fail);   // 4 %: not contended
	CHECK(Gates::EvaluateG10b(MakeWindow(1, 0.95, 0.0, 0.5, 0.0, 0.75), "t").Result == EGateResult::Fail);   // cpu_eff decides anyway
	// M10 fix round 0: a thread in neither census (born and gone inside the window) counts as foreign: 0.3 % + 0.3 % fails.
	CHECK(Gates::EvaluateG10b(MakeWindow(1, 1.0, 0.003, 0.5, 0.0), "t").Result == EGateResult::Pass);
	const FGateReport Hidden = Gates::EvaluateG10b(MakeWindow(1, 1.0, 0.003, 0.5, 0.003), "t");
	CHECK(Hidden.Result == EGateResult::Fail);
	REQUIRE(Hidden.Failures.size() == 1);
	CHECK(Hidden.Failures.front().find("unattributed") != std::string::npos);
	CHECK(Gates::EvaluateG10b(FCensusWindow(), "t", &Why).Result == EGateResult::Skip);
	CHECK(Why == Gates::EG10bSkip::NoCensus);
	CHECK(std::string(Gates::ToString(Gates::EG10bSkip::Contended)) == "contended");
}

TEST_CASE("PhysicsTest.Measurement.StampKey")
{
	// 02 12.4: the key's canonical JSON (keys sorted, no whitespace) is what Tools/ptbench/stamps.py hashes too; the -ini: and
	// -ExecCmds= switches are sorted and lose their quotes.
	FConformanceStampKey Key;
	Key.BenchExeSha256 = std::string(64, 'a');
	Key.PhysicsSha256 = std::string(64, 'b');
	Key.TolerancesSha256 = std::string(64, 'c');
	Key.GateSuiteVersion = 9;
	Key.ConfigOverrides = ExtractConfigOverrides("-Backend=Jolt -ini:PhysicsTest:[S]:K=1 -ExecCmds=\"p.A 1, p.B 2\" -Profile=Normalized /ini:X:[T]:Y=2");
	REQUIRE(Key.ConfigOverrides.size() == 3);
	CHECK(Key.ConfigOverrides[0] == "-ExecCmds=p.A 1, p.B 2");
	CHECK(Key.ConfigOverrides[1] == "-ini:PhysicsTest:[S]:K=1");
	CHECK(Key.ConfigOverrides[2] == "-ini:X:[T]:Y=2");
	const std::string Canonical = Key.ToCanonicalJson();
	CHECK(Canonical == "{\"backend_module_sha256\":\"\",\"bench_exe_sha256\":\"" + std::string(64, 'a')
		+ "\",\"config_overrides\":[\"-ExecCmds=p.A 1, p.B 2\",\"-ini:PhysicsTest:[S]:K=1\",\"-ini:X:[T]:Y=2\"],\"gate_suite_version\":9,\"physics_sha256\":\""
		+ std::string(64, 'b') + "\",\"tolerances_sha256\":\"" + std::string(64, 'c') + "\",\"variant_sha256\":\"\"}");
	CHECK(Key.ComputeSha256().size() == 64);
	CHECK(Key.ToPath("D:/x").filename().string() == Key.ComputeSha256() + ".json");
	FConformanceStampKey Other = Key;
	Other.VariantSha256 = std::string(64, 'd');
	CHECK(Other.ComputeSha256() != Key.ComputeSha256());
	CHECK(ExtractConfigOverrides("-Backend=Null -Scenario=S00.FreeFall").empty());
}
