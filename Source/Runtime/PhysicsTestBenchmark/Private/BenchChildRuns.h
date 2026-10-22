// BenchChildRuns.h - child bench runs that other modes start (private to PT::Benchmark; BenchMultiRun.cpp). [확장 M10]
#pragma once

#include "BenchmarkRunner.h"
#include "ConformanceGate.h"

#include <filesystem>
#include <string>

namespace PhysicsTest
{
	// The run -Conformance starts for G10b (03 M10 task 2, 02 14.3): a fresh process of this exe on the conformance target's
	// backend, variant, physics profile, -ConfigDir and -ini: / -ExecCmds switches, at T = 1 with the measurement protocol's
	// pre-roll and a record-only host check (another process's load must not make it exit 4). Its census window decides.
	inline constexpr const char* G10bConformanceScenario = "M3.BoxPile";
	inline constexpr int32 G10bConformanceBodies = 1000;
	inline constexpr int32 G10bConformanceFrames = 300;
	// M10 fix round 0: a window with less own CPU time than G10b's minimum (a fast backend such as Null) runs again with its
	// frames scaled to about this much own CPU time, at most G10bConformanceMaxFrames (fix round 1: own CPU time, was wall).
	inline constexpr double G10bConformanceTargetSeconds = 0.15;
	inline constexpr int32 G10bConformanceMaxFrames = 30000;
	// M10 fix round 1: at most this many runs; a contended window (no verdict, Gates/ThreadGate.h) runs again after a pause of
	// this many ms times the attempt number (a desktop or parallel CTest benches on the same core come and go).
	inline constexpr int32 G10bConformanceAttempts = 3;
	inline constexpr uint32 G10bConformanceContendedPauseMs = 1000;

	// The child's G10b report (Fail when the child did not end with exit.json 0 or wrote no G10b, and - M10 fix round 1 - when
	// its last run is still a Skip: a stamp needs a G10b verdict); OutDir (and OutDir_2, OutDir_3 for the next runs) receives it.
	[[nodiscard]] FGateReport RunConformanceG10b(const FBenchRunSpec& Spec, const std::filesystem::path& OutDir, const std::string& Label);

	// M10 fix round 0 (02 14.2, 12.4): the ABI gates a DLL backend's whole -Conformance suite runs, each in a fresh process of
	// this exe (-Conformance -Gates=<id> with the target's backend, variant, profile and configuration switches): one Init per
	// process, the PT_MODULE_NO_REINIT rule (docs/ChaosBackendGuide.md 11). A0-A7 and A10 (A8 is M14, A9 is
	// Tools/check_modules.py).
	inline constexpr const char* ConformanceAbiGateIds[] = { "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A10" };

	// The child's report of GateId (Fail when the child wrote no conformance.json entry for it or - M10 fix round 1 - reported Skip);
	// OutDir receives its run.
	[[nodiscard]] FGateReport RunConformanceAbiGate(const FBenchRunSpec& Spec, const std::filesystem::path& OutDir, const std::string& GateId,
		const std::string& Label);
}
