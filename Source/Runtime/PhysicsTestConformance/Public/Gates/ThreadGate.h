// ThreadGate.h - G10b, the single-thread proof (02_Architecture 14.3, 03_WorkPlan M10 task 2). [확장 M10]
// A T = 1 run passes when its census window (BenchCore.h FCensusWindow, census #1 before frame 0 and #2 after the last frame)
// shows that
//   - the process kept the one core it was given busy:  cpu_eff in [G10bCpuEffMin, G10bCpuEffMax], where cpu_eff = the cycles
//     of this process's threads / (wall x T - the cycles other processes and interrupts took on the mask's CPUs); and
//   - the timing thread did the work:                    the other threads of this process (engine workers, a UE TaskGraph,
//     the loader's thread pool) used less than G10bForeignMax of them, counting the process cycles no census thread explains
//     (a thread born and gone inside the window: unattributed, M10 fix round 0).
// Limits (docs/TimingContract.md 7.1): cpu_eff is ~1 by construction for a spinning timing thread, so it catches a timing
// thread that blocks or sleeps, not a worker; the foreign + unattributed bound carries the single-thread proof.
// M10 fix round 1 (a starved core: parallel CTest benches pinned to CPU 0): the 0.5 % bound is a share of the process's own
// cycles, so the window's minimum is the process's own CPU time (G10bMinOwnSeconds), not its wall time; and a failure in a
// contended window (other processes took more than G10bContendedOtherMax of the mask) is a verdict only when the extra cycles
// also exceed 0.5 % of the mask's busy cycles - a thread that runs in proportion to wall time (the prof build's Tracy client)
// and the census reads' fixed residue grow against own as the process loses its core, real work does not. Otherwise the
// window is inconclusive: Skip with the values. A pass in a contended window stands (the bound held against the smaller own).
// A Skip is never a proof: the run is headline-valid only when its Skip is the short-window kind (P0's sub-ms windows; the
// stamp's G10b run judges the backend then), and -Conformance turns every Skip of its G10b run into Fail (a valid stamp
// always carries a G10b Pass). At T > 1 the gate reports Skip with the values (scaling runs use the same census for cpu_eff,
// 02 13 perf). The bench evaluates it for every run (manifest gates[]) and -Conformance runs it on a fresh T = 1 bench run of
// M3.BoxPile 1k, so a conformance stamp certifies it for the binary (02 12.4).
#pragma once

#include "BenchCore.h"
#include "ConformanceGate.h"

namespace PhysicsTest::Gates
{
	inline constexpr double G10bCpuEffMin = 0.98;
	inline constexpr double G10bCpuEffMax = 1.02;
	inline constexpr double G10bForeignMax = 0.005;
	// A window with less own CPU time than this has too few cycles for a 0.5 % bound on another thread: Skip with the values
	// instead of a verdict. M10 fix round 0: 50 ms (was 10 ms) - the census bracket's own reads leave up to ~0.1 ms (~0.35 M TSC
	// cycles) of process time no thread carries even after the measured baseline [V], and 0.5 % of 50 ms is 0.87 M cycles.
	// M10 fix round 1: own CPU time (was wall time), so a run that got a fraction of its core still has 50 ms of its own cycles.
	inline constexpr double G10bMinOwnSeconds = 0.050;
	// A window is contended when other processes, interrupts and DPCs took more than this share of the mask's capacity (the
	// default -ContaminationPct, 5 % of one core per core: exit code 4's bound).
	inline constexpr double G10bContendedOtherMax = 0.05;

	// Why a G10b report is Skip (EvaluateG10b's OutSkip): the bench's headline rule and -Conformance's retry read it.
	enum class EG10bSkip : uint8
	{
		None,           // a verdict (Pass or Fail)
		MultiThread,    // T > 1: the values are recorded, G10b judges T = 1 only
		NoCensus,       // the census window could not be measured
		ShortWindow,    // own CPU time below G10bMinOwnSeconds
		Contended,      // the bound failed against own but not against the mask's busy cycles in a contended window
	};
	[[nodiscard]] const char* ToString(EG10bSkip Skip);

	// Label: the target text of the report ("Jolt.Default / Normalized").
	[[nodiscard]] FGateReport EvaluateG10b(const FCensusWindow& Window, const std::string& Label, EG10bSkip* OutSkip = nullptr);
}
