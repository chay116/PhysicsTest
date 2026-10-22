// BenchPreflight.h - BenchPreflight::Run (the A.12 sketch's FPreflight): the gates a bench run passes before Prepare
// (02_Architecture 12.2 step 7, 14.3; 03_WorkPlan M7, A.12). [확장 M7: G0, G9, G10a; 확장 M10: the conformance stamp]
//   G0   build: the adapter TU's runtime self-report (PtBackendInfo::BuildFlagsJson) = the facts compile_commands.json records
//        for the same adapter target (BuildFlagsSha.h, PT_BUILD_FLAGS_G0_JSON) = the flag profile's expectation; the SDK
//        group's TUs have the profile's /arch, /fp:fast, the CRT and iterator debug level of the configuration; PhysX's own
//        TUs are the configuration's PhysX build; Jolt's configuration string has no Profile / Debug Renderer / FP
//        Exceptions; no violation anywhere in BuildFlags.json; the bench exe's own TUs (harness) have no /arch.
//   G9   effective readback: the run's own READBACK keys equal their resolved values (Gates::CheckEffectiveReadback with
//        tolerances.json); listeners.installed and the drawer keys are G10a's; shape.wrapping is checked (= 0) only for
//        performance scenes (a gate scene such as S03 needs wrappers by design).
//   G10a bench hygiene: a bench-mode run of instrumentation tier 0 has no listener (listeners.installed 0), no debug drawer
//        (every *.debug_drawer key 0) and its PtSceneDesc carries tier 0 and RunMode Bench. An instrumented or non-bench
//        run is not refused: the report says why its timing is not headline-valid.
// A failed gate refuses the run: exit code 2, status preflight_failed, Prepare and the frame loop are skipped.
#pragma once

#include "BenchmarkRunner.h"
#include "ConformanceGate.h"
#include "Gates/PhysicsGates.h"
#include "Physics/EffectiveSettings.h"

#include <string>
#include <vector>

namespace PhysicsTest
{
	class FPhysScene;
	struct FBackendInfo;

	namespace Gates
	{
		class FTolerances;
	}

	namespace BenchPreflight
	{
		struct FInputs
		{
			const FBenchRunSpec* Spec = nullptr;
			const FResolvedPhysicsConfig* Config = nullptr;
			const FBenchConfig* Files = nullptr;
			const IPhysicsTestScenario* Scenario = nullptr;
			const FScenarioParams* Params = nullptr;
			const FPhysScene* Scene = nullptr;
			const std::vector<FEffectiveSetting>* Settings = nullptr;
			const Gates::FTolerances* Tolerances = nullptr;   // nullptr: G9 fails (tolerances.json did not load)
			std::string TolerancesError;
			double DtEngine = 0.0;
		};

		struct FResult
		{
			std::vector<FGateReport> Reports;   // G0, G9, G10a
			bool bPassed = true;
			std::vector<std::string> TimingInvalidReasons;   // what G10a found that keeps the timing off the headline
			// M9 (02 10): every effective key of the run next to its resolved expectation (G9's rule) - preflight_diff.json.
			std::vector<Gates::FReadbackDiffRow> Diff;
			// A test-only -InjectPreflightFault=g9:<key> that cannot fail G9 (no such key, or G9 does not compare it on this
			// scene): the run is refused as a usage error, so a mistyped injection never passes silently (M8, M7 review).
			std::string InjectionError;
		};

		[[nodiscard]] FResult Run(const FInputs& Inputs);

		// G0 alone (the bench preflight, and the automation tests): Info is the backend module's, BackendName its registry name.
		[[nodiscard]] FGateReport RunG0(const FBackendInfo& Info, const std::string& BackendName, const std::vector<FEffectiveSetting>& Settings,
			const std::string& InjectFault);
	}
}
