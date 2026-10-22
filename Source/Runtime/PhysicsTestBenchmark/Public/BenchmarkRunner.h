// BenchmarkRunner.h - PT::Benchmark, the bench run of one scenario on one backend in this process (02_Architecture 9, 12,
// 14.3, A.12; 03_WorkPlan M3, M6, M7). [MVP M3 minimal; M6 -Conformance; 확장 M7 the full run order, host controls,
// recorders, preflight, exit.json, the multi-process gates G8 / G11; 확장 M10 TSC, census, stamps]
//
//   PhysicsTestBench.exe -Backend=Jolt [-BackendProfile=Default] [-Profile=Normalized] -Scenario=M3.BoxPile -Param:N=5000
//                        [-Frames=600] [-Threads=1] [-Affinity=0x1] [-RunMode=Bench|Behavior|Viz]
//                        [-Instrument=none|counters,phases,alloc,contacts,native_debug_draw]
//                        [-Repeat=<r>] [-Order=<o>] [-BlockSeed=<s>] [-HashEvery=0] [-StatesEvery=0] [-CountersEvery=0]
//                        [-CaptureEvery=0] [-PreRoll=0.5] [-HostCheck=enforce|record] [-ContaminationPct=5]
//                        [-Out=<run dir>] [-Session=<name>] [-ConfigDir=<dir>] [-ini:PhysicsTest:[Section]:Key=Value,...]
//                        [-ExecCmds="cvar value, ..."] [-RequireEffectiveKey=key,...]
//                        test only: [-InjectEngineErrorBits=0x..] [-InjectPreflightFault=g0|g9:<key>|listener|drawer]
//                                   [-InjectSceneInstrumentFlags=0x8] [-InjectFatal] [-InjectTeardownFatal]
//                                   [-InjectStatePerturbation=<frame>] [-InjectTickAllocation=<frame>]
//                                   [-InjectSilentExit=<frame>] [-InjectForeignWork=<ms>]
//   PhysicsTestBench.exe -Gate=G8 [-Runs=3] <run arguments>   DET1: N fresh processes of the run, equal final h_native
//   PhysicsTestBench.exe -Gate=G11 <run arguments>            contacts off / on, every frame's hashes bit-identical
//                        test only: [-InjectGateFault=<frame>] perturbs one child (G8: the last run, G11: contacts on), so
//                                   the comparison must fail (the negative control)
//   PhysicsTestBench.exe -BusyHelper=<seconds> -Affinity=0x1  the exit-4 test load (spins the mask at HIGH priority)
//   PhysicsTestBench.exe -List | -Describe -Backend=.. | -Conformance -Backend=.. | -SceneHashes=<path>   (M3 / M6)
//   PhysicsTestBench.exe -AnalyticReferences   the C++ analytic references as JSON (M8: Tools/ptbench/analytic.py must agree
//                                              within 1e-9, CTest PhysicsTest.Py.AnalyticMatchesCpp)
//   PhysicsTestBench.exe -AbiOverhead [-Backend=Null]   the ABI call overhead on the TSC as JSON (M10: Null, 0 bodies, 4 calls
//                                              per frame, the median over batches; the gate is < 100 ns per frame)
//   M10: -Conformance also runs G10b (a fresh T = 1 bench run of M3.BoxPile 1k with the same configuration) and writes the
//        conformance stamp to -StampDir (default Saved/Conformance); a run records the stamp of its own key (conformance_stamp)
// -List, -Describe, -SceneHashes, -AnalyticReferences and -AbiOverhead print pure JSON on stdout: every log line goes to stderr
// in these modes (M6).
//
// Run order (02 12.2; M7, M10): parse -> run directory and the Fatal hook (exit.json 3) -> host controls (affinity, priority,
// power throttling opt-out, timeBeginPeriod; before any SDK init or LoadLibrary) and the first TSC / QPC pair -> config (Base
// -> profile -> variant -> command line) -> backend load + InitGamePhys -> thread census #1 (census_init) -> engine-option
// preflight -> scene (BuildScene + flush, desc_sha256) -> effective settings + preflight (required keys, G0, G9, G10a) and the
// stamp of this run's key (recorded, never refused here: the runner refuses) -> Prepare -> spin pre-roll on the T cores ->
// census window begin -> frame 0 -> timed loop (TSC buckets; recorders between frames, never inside the timed buckets) ->
// census window end (G10b, cpu_eff) -> the run's TSC calibration (every bucket converted again) -> untimed reads -> outputs,
// then exit.json last -> teardown (scene, TermGamePhys; teardown.json); a PT_MODULE_UE_RUNTIME module ends the process with
// TerminateProcess.
// The Fatal hook writes exit.json (3) only until the run's final exit.json; a Fatal in the teardown goes to teardown.json
// and the process ends with the run's own code (DL52).
//
// Exit codes (02 12.2), always also written to exit.json in the run directory (the runner trusts it over the process exit
// code): 0 ok, 2 gate / preflight failure or a usage error (exit.json category "usage" / "preflight" / "gate"), 3 engine
// error (also a Fatal: the hook writes exit.json first), 4 contaminated host (other processes busy on the run's cores - the
// mask's CPUs and their SMT siblings - above -ContaminationPct of one core per core, -HostCheck=enforce), 5 capacity
// exceeded (only the backend's errors.capacity_bits), 6 backend unavailable. Precedence when several apply: 2 (no loop ran),
// 3, 5, 4.
// Every timing stays UNOFFICIAL unless the run is headline-valid, its conformance stamp is valid and the power plan is High
// performance / Ultimate (M10; M11 runs the first such session): the manifest says so ("unofficial", timing_label).
#pragma once

#include "CoreTypes.h"
#include "Physics/PhysSceneStats.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsTestScenario.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace PhysicsTest
{
	struct FResolvedPhysicsConfig;

	enum class EBenchExit : int32
	{
		Ok = 0,
		GateFailed = 2,
		EngineError = 3,
		Contaminated = 4,
		Capacity = 5,
		BackendUnavailable = 6,
	};
	[[nodiscard]] const char* ToString(EBenchExit Exit);

	// What -HostCheck does with a contaminated host (03 M7: exit code 4).
	enum class EHostCheckPolicy : uint8
	{
		Enforce,   // exit 4 (the protocol default)
		Record,    // record the load only (CTest on a desktop in use; the manifest marks the timing invalid)
	};

	struct FBenchRunSpec
	{
		std::string Backend;                 // registry name (Null, NullDll, Jolt, ...)
		std::string BackendProfile;          // backend variant (Config/Profiles/Backend/<Backend>.<Variant>.ini); "" = Default if
		                                     // that file exists, else none
		std::string PhysicsProfile = "Normalized";   // Config/Profiles/<Profile>.ini
		std::string Scenario;
		FScenarioParams Params;              // -Param:Name=Value (repeatable)
		int32 Frames = 0;                    // 0 = the scenario's default
		int32 Threads = 1;                   // T, the calling thread included
		std::filesystem::path OutDir;        // the run directory; empty = <repo>/Saved/Results/<Session>/<run id>
		std::string Session = "adhoc";
		std::filesystem::path ConfigDir;     // empty = the repository's Config/
		std::string CommandLine;             // the full command line: -ini: and -ExecCmds reach the resolver
		std::vector<std::string> ExtraRequiredKeys;   // -RequireEffectiveKey=a,b: required by this run's preflight on top of
		                                              // the backend's set (a variant check; CTest drives the preflight path)
		// ---- M7 run options (they override the profile's [/Script/PhysicsTest.PhysicsTestSettings] values) ----
		uint64 AffinityMask = 0;             // -Affinity=0x15; 0 = the first T P-core logical CPUs (02 9.6: 0x1, 0x5, 0x15, ...)
		std::optional<EPhysicsRunMode> RunMode;     // -RunMode=Bench|Behavior|Viz
		std::optional<uint32> InstrumentFlags;      // -Instrument=none|counters,phases,alloc,contacts,native_debug_draw
		std::optional<int32> HashEvery;             // 0 = the final frame only (headline)
		std::optional<int32> StatesEvery;           // 0 = no states.ptst
		std::optional<int32> CountersEvery;         // 0 = no per-frame GetStepStats counters
		std::optional<int32> CaptureEvery;          // contacts.ptcn frames (with -Instrument=contacts; 0 there = every frame)
		int32 RepeatIndex = 0;               // -Repeat=<r>: the runner's repeat index (manifest order group)
		int32 OrderIndex = 0;                // -Order=<o>: the position in the runner's shuffled block
		uint64 BlockSeed = 0;                // -BlockSeed=<s>: the runner's shuffle seed
		std::filesystem::path StampDir;      // -StampDir=<dir> (M10): where -Conformance writes stamps and a run looks for its own;
		                                     // empty = <repo>/Saved/Conformance
		double PreRollSeconds = 0.5;         // -PreRoll=<s>: spin pre-roll on the T cores before frame 0 (02 9.5)
		EHostCheckPolicy HostCheck = EHostCheckPolicy::Enforce;
		double ContaminationPct = 5.0;       // -ContaminationPct: % of one core other processes may use per core of the run (the
		                                     // limit is this x the cores the mask touches; SMT siblings belong to their core)
		// ---- test only ----
		uint32 InjectEngineErrorBits = 0;    // -InjectEngineErrorBits=0x200: OR-ed into the run's engine error bits after the
		                                     // loop (CTest drives exit codes 3 / 5 through the exe); the manifest says so.
		                                     // With -Conformance: OR-ed into every physics-gate frame, so G12 fails (M6 round 1)
		std::string InjectPreflightFault;    // -InjectPreflightFault=g0|g9:<key>|listener|drawer: corrupts what the preflight
		                                     // gate reads (G0: the adapter self-report; G9: one READBACK value; G10a: a
		                                     // listener or a debug drawer reported as attached), so exit 2 is shown per gate
		uint32 InjectSceneInstrumentFlags = 0;   // -InjectSceneInstrumentFlags=0x8: OR-ed into the PtSceneDesc only - the
		                                         // backend installs its listener while the run declares tier 0 (G10a, real)
		bool bInjectFatal = false;           // -InjectFatal: a Fatal log after the preflight (exit.json from the Fatal hook)
		bool bInjectTeardownFatal = false;   // -InjectTeardownFatal: a Fatal log in the teardown, after exit.json (teardown.json
		                                     // records it; exit.json and the process exit code keep the run's code)
		int32 InjectStatePerturbationFrame = -1;   // -InjectStatePerturbation=<f>: a 1e-3 cm/s velocity change of the last
		                                           // dynamic body after frame f (between frames; the G8 / G11 negative control)
		int32 InjectGateFaultFrame = -1;     // -InjectGateFault=<f> with -Gate=: the gate passes -InjectStatePerturbation=<f> to
		                                     // one child only (G8: the last run; G11: contacts on), so the comparison must fail
		int32 InjectTickAllocationFrame = -1;   // -InjectTickAllocation=<f>: one operator new inside frame f's Tick (the scene's
		                                        // PreTick delegate) - the positive control of the allocation hook (M8)
		int32 InjectSilentExitFrame = -1;    // -InjectSilentExit=<f>: the process ends with TerminateProcess(0) after frame f,
		                                     // before any output or exit.json - a run that died half-way with a raw exit code
		                                     // of 0, which the M8 runner must report as failed (exit.json missing)
		int32 InjectForeignWorkMs = -1;      // -InjectForeignWork=<ms>: a helper thread started right after census #1 spins <ms>
		                                     // ms of its own CPU time and exits (joined after census #2) - another thread's work
		                                     // at T = 1, the G10b negative control (M10 fix round 0): G10b must fail
		// ---- modes ----
		bool bList = false;
		bool bDescribe = false;
		bool bAnalyticReferences = false;    // -AnalyticReferences (M8)
		bool bAbiOverhead = false;           // -AbiOverhead (M10): the ABI call overhead of -Backend (default Null) as JSON
		// -Conformance (M6, 02 14.3): the physics gate suite on this backend / variant / profile -> conformance.json in the
		// run directory; exit 0 when every gate passes or skips, 2 when one fails. -Gates=G1,G9 selects (default: all).
		bool bConformance = false;
		std::vector<std::string> Gates;
		// -SceneHashes=<path> (M6): writes / updates the golden scene hashes of every gate scene (scene_sha256, and
		// desc_sha256 of this build configuration per profile) at <path>; the other configuration's entries are kept.
		std::filesystem::path SceneHashesPath;
		// -Gate=G8 / -Gate=G11 (M7): the multi-process gates. G8 (DET1) runs the run -Runs=N times (default 3) in fresh
		// processes; G11 runs it twice with one result-neutral knob changed and compares every frame's hashes.
		std::string MultiRunGate;
		int32 GateRuns = 3;
		// -G11Knob= (M9; 02 14.3 G11, 03 M9): contacts (default, M7: capture off / on), variants:<A>,<B> (the backend variants
		// A and B, e.g. variants:Default,StaticAabbOff for Bullet.StaticAabbOff), viz (Bench mode without instrumentation / Viz
		// mode with -Instrument=native_debug_draw: an attached debug drawer must not change the result, e.g. Bullet's).
		std::string G11Knob;
		double BusyHelperSeconds = 0.0;      // -BusyHelper=<s> (M7): the exit-4 test load

		// UE-style switches (FParse). False + OutError for a malformed value (e.g. -Threads=4x, -Frames=0). Out.OutDir is set
		// before any later check fails, so the caller can still write exit.json (category "usage").
		static bool FromCommandLine(const char* CommandLine, FBenchRunSpec& Out, std::string& OutError);
		// Only the -Param:Name=Value tokens (FromCommandLine uses it; the viewer too, which has its own -Frames meaning).
		// False + OutError for a malformed token.
		static bool ParseScenarioParams(const char* CommandLine, FScenarioParams& OutParams, std::string& OutError);
	};

	// -Instrument tier names <-> PT_INSTR_* bits ("counters,contacts" -> 0x9; "none" -> 0). False for an unknown name.
	bool ParseInstrumentFlags(const std::string& Text, uint32& OutFlags, std::string& OutError);
	[[nodiscard]] std::vector<std::string> GetInstrumentNames(uint32 Flags);

	// One row of timings.csv per frame (02 12.3), kept in a pre-sized array: the timed loop never allocates.
	struct FFrameRecord
	{
		FFrameTiming Timing;
		double SimTime = 0.0;
		int32 AwakeBodies = -1;
		int32 ContactPairs = -1;             // GetStepStats when a counters frame (CountersEvery), else -1
		int32 ContactPoints = -1;
		int32 Islands = -1;
		int64 SolverNs = -1;                 // sum of the backend's phase timers (PT_INSTR_PHASES, PT_CAP_PHASE_TIMERS), -1 = none
		// M10: two TSC and two QPC reads around FPhysScene::Tick (outside the buckets): the whole Tick in TSC ticks, and the TSC /
		// QPC values at its end (timings.csv tick_tsc_ticks, tsc_end, qpc_end; Timing.FrameQpcTicks = the Tick's QPC ticks).
		uint64 TickTsc = 0;
		uint64 TscEnd = 0;
		uint64 QpcEnd = 0;
	};

	class FTimingRecorder
	{
	public:
		void Reserve(int32 Frames) { Records.reserve(static_cast<size_t>(Frames)); }
		void Record(const FFrameRecord& Record) { Records.push_back(Record); }
		// M10: every bucket's ns value again from its raw TSC ticks with the run's calibration (02 9.3: the TSC is calibrated
		// against QPC over the whole run); FrameNs stays PushNs + StepNs + PullNs. Also the ns per tick WriteCsv uses for tick_ns.
		void Recalibrate(double InNsPerTick);
		[[nodiscard]] double GetNsPerTick() const { return NsPerTick; }
		// frame, sim_time_s, num_steps, push_ns, setup_ns, start_ns, wait_ns, end_ns, step_ns, pull_ns, frame_ns, frame_qpc_ticks,
		// solver_ns, awake_bodies, contact_pairs, contact_points, islands, error_bits, and (M10) frame_tsc_ticks, tick_ns,
		// tick_tsc_ticks, tsc_end, qpc_end. A value the run did not measure is empty (solver_ns without phase timers), a counter
		// it could not get is -1 (no counters frame, no listener).
		bool WriteCsv(const std::filesystem::path& Path) const;
		[[nodiscard]] const std::vector<FFrameRecord>& GetRecords() const { return Records; }

	private:
		std::vector<FFrameRecord> Records;
		double NsPerTick = 0.0;
	};

	// The resolved physics configuration of a run: Base -> Profile -> backend variant -> command line (02 10).
	struct FBenchConfig
	{
		std::filesystem::path ProfilePath;
		std::filesystem::path VariantPath;   // empty without a variant
		std::string VariantName;
		std::string VariantLabel;            // [PhysicsTest.Variant] Label (the series label of a table)
		std::vector<std::string> FidelityNotes;   // [PhysicsTest.Variant] +FidelityNotes (written into every manifest)
		std::string ProfileSha256;           // SHA-256 of the profile file's canonical text
		std::string VariantSha256;           // variant_sha256 ("" without a variant)
	};
	// Also applies the spec's run options (Threads, RunMode, InstrumentFlags, HashEvery, CountersEvery, CaptureEvery,
	// AffinityMask) on top of the profile's values: run options are not physics keys (physics_sha256 excludes them, 02 10).
	bool LoadBenchConfig(const FBenchRunSpec& Spec, FResolvedPhysicsConfig& OutConfig, FBenchConfig& OutFiles, std::string& OutError);
	// <repo>/Config unless -ConfigDir= was given.
	[[nodiscard]] std::filesystem::path GetDefaultConfigDir();
	// <repo>/Saved/Results
	[[nodiscard]] std::filesystem::path GetDefaultResultsDir();
	// <repo>/Saved/Conformance (M10: the conformance stamps, 02 12.4)
	[[nodiscard]] std::filesystem::path GetDefaultStampDir();

	// exit.json (schema physicstest.exit/1, M7): written and flushed last, after every other output (02 12.2 step 12).
	struct FExitRecord
	{
		EBenchExit Code = EBenchExit::Ok;
		std::string Status = "ok";           // ok, usage_error, preflight_failed, gate_failed, engine_error, fatal,
		                                     // contaminated_host, capacity_exceeded, backend_unavailable
		std::string Category;                // "", "usage", "preflight", "gate", "engine", "host", "capacity", "backend"
		std::string Reason;
		std::string RunId;
		bool bTerminateProcess = false;      // a PT_MODULE_UE_RUNTIME module is loaded: the process ends with TerminateProcess
		                                     // (after the teardown, or right after this file for a Fatal), never a CRT exit
		std::vector<std::string> Outputs;    // the files written before exit.json
		bool WriteAndFlush(const std::filesystem::path& OutDir) const;
	};

	// The Fatal hook of one run, -Conformance or -Gate= (M7; public since M8 so that BenchMultiRun.cpp arms it too). While
	// alive, a Fatal writes <OutDir>/exit.json (3, status fatal) before the process ends; with a PT_MODULE_UE_RUNTIME module
	// loaded the process then ends with TerminateProcess. After EnterTeardown the verdict is final (DL52): a Fatal goes to
	// teardown.json and the process ends with the run's code. Leaving the scope disarms it (the process-wide fallback of
	// FBenchmarkRunner::InstallFatalHook stays).
	class FFatalExitScope
	{
	public:
		FFatalExitScope(const std::filesystem::path& OutDir, const std::string& RunId);
		~FFatalExitScope();
		FFatalExitScope(const FFatalExitScope&) = delete;
		FFatalExitScope& operator=(const FFatalExitScope&) = delete;
		// Right after the final exit.json: a later Fatal never rewrites it.
		void EnterTeardown(EBenchExit RunExit);
	};

	class FBenchmarkRunner
	{
	public:
		// One run (02 12.2); writes the outputs and exit.json into the run directory and returns the exit code. With a
		// UE_RUNTIME module it ends the process with TerminateProcess(GetCurrentProcess(), code) after the teardown instead
		// of returning (no UE teardown during the CRT's DLL detach).
		EBenchExit Run(const FBenchRunSpec& Spec);
		// -Gate=G8 / G11 (M7): fresh child processes of this exe (FProcessSpawner), compared; gate_<id>.json + exit.json.
		static EBenchExit RunMultiProcessGate(const FBenchRunSpec& Spec);
		// -BusyHelper=<s>: spins the -Affinity CPUs at HIGH_PRIORITY_CLASS (the exit-4 CTest's foreign load), returns 0.
		static EBenchExit RunBusyHelper(const FBenchRunSpec& Spec);
		// -List: scenarios with their parameters and the registered backends.
		static EBenchExit List();
		// -Describe: the backend's info, capabilities, required keys and the effective settings of a scene built with the
		// resolved profile (the scenario's scene when -Scenario is given, else an empty scene), as JSON on stdout.
		static EBenchExit Describe(const FBenchRunSpec& Spec);
		// -Conformance (M6): the physics gates on the backend / variant / profile of Spec; writes conformance.json (schema
		// physicstest.conformance/1) and exit.json into the run directory and prints its path. 0 = every gate passed or skipped.
		static EBenchExit Conformance(const FBenchRunSpec& Spec);
		// -SceneHashes=<path> (M6): the golden scene hashes of the gate scenes (and S00.FreeFall), built on Null.
		static EBenchExit WriteSceneHashes(const FBenchRunSpec& Spec);
		// -AnalyticReferences (M8): the Analytic:: references (AnalyticReference.h) of the gate scenes' canonical inputs as JSON
		// on stdout (schema physicstest.analytic/1); Tools/ptbench/analytic.py recomputes every case within 1e-9.
		static EBenchExit AnalyticReferences();
		// -AbiOverhead (M10): AbiGates::MeasureCallOverhead on -Backend (default Null) as JSON on stdout (physicstest.abi_overhead/1);
		// exit 0 when the median is below 100 ns per frame (or in a Debug build, which records it), 2 above.
		static EBenchExit AbiOverhead(const FBenchRunSpec& Spec);
		// Once, first thing in main (M7 review, M8): the process-wide Fatal callback. Outside a run (-Describe, -List,
		// -SceneHashes) a Fatal with a PT_MODULE_UE_RUNTIME module loaded ends the process with TerminateProcess (never the CRT
		// exit that runs DLL_PROCESS_DETACH); -Conformance and -Gate= also write their exit.json (3, status fatal) first.
		static void InstallFatalHook();
		// True when this process loaded a PT_MODULE_UE_RUNTIME module (every mode then ends with TerminateProcess, 02 12.2).
		[[nodiscard]] static bool HasUeRuntimeModule();
		// Flushes stdio and ends the process with TerminateProcess(GetCurrentProcess(), Code), logged first with Why (default:
		// a PT_MODULE_UE_RUNTIME module is loaded).
		[[noreturn]] static void TerminateProcessNow(int32 Code, const char* Why = nullptr);
	};

	// One gate scene's golden hashes (Config/Golden/scene_hashes.json, M6): scene_sha256 (build-independent) and desc_sha256
	// per physics profile for the build configuration this code was compiled in (PT_BUILD_CONFIGURATION), with the scene's
	// default parameters and frames, built on Null (desc_sha256 is engine independent, DL43).
	struct FSceneHashEntry
	{
		std::string Scene;
		int32 Version = 0;
		std::string SceneSha256;
		int32 DefaultFrames = 0;
		std::vector<std::pair<std::string, std::string>> DescSha256ByProfile;   // (profile, desc_sha256); S08 has UEParity only
	};
	// Every EScenarioKind::Gate scenario. False + OutError when a profile does not load.
	bool ComputeGateSceneHashes(const std::filesystem::path& ConfigDir, std::vector<FSceneHashEntry>& Out, std::string& OutError);
}
