// BenchmarkRunner.h - PT::Benchmark, the bench run of one scenario on one backend in this process (02_Architecture 12,
// A.12). [MVP M3 minimal: FBenchRunSpec subset, FixedStep loop, timings.csv / manifest.json / effective_settings.jsonl;
// 확장 M7: host controls, recorders (hashes.csv, states.ptst, contacts.ptcn), preflight, exit.json; 확장 M10: stamps]
//
//   PhysicsTestBench.exe -Backend=Jolt [-BackendProfile=Default] [-Profile=Normalized] -Scenario=M3.BoxPile -Param:N=5000
//                        [-Frames=600] [-Threads=1] [-Out=<dir>] [-Session=<name>] [-ConfigDir=<dir>]
//                        [-RequireEffectiveKey=key,...] [-InjectEngineErrorBits=0x...] [-ini:PhysicsTest:[Section]:Key=Value,...]
//                        [-ExecCmds="cvar value, ..."]
//   PhysicsTestBench.exe -List
//   PhysicsTestBench.exe -Describe -Backend=Jolt [-BackendProfile=..] [-Profile=..] [-Scenario=.. -Param:..]
//   PhysicsTestBench.exe -Conformance -Backend=PhysX [-BackendProfile=UE4Compat] [-Profile=UEParity] [-Gates=G1,G9]
//                        [-Out=<dir>] [-ConfigDir=<dir>] [-InjectEngineErrorBits=0x...]       (M6: conformance.json; a
//                        PT_MODULE_NO_REINIT backend: one ABI gate or the physics gates per process, else exit 2)
//   PhysicsTestBench.exe -SceneHashes=<path>                                                  (M6: Config/Golden/scene_hashes.json)
// -List, -Describe and -SceneHashes print pure JSON on stdout: every log line goes to stderr in these modes (M6, for the
// M7 / M8 tools).
//
// Exit codes (02 12.2): 0 ok, 2 gate / preflight failure (in M3 also: an invalid command line, profile, scenario or
// parameter; HashEvery / CountersEvery > 0, which need the M7 recorder; the backend rejecting the variant's engine
// options; a missing required effective key - the last two write manifest.json with status preflight_failed and skip
// Prepare and the frame loop), 3 engine error (an ABI failure after the preflight stops the process through Fatal with
// code 3; an engine error bit outside the backend's capacity bits), 5 capacity exceeded (only capacity bits: the bits
// the backend lists in the effective key errors.capacity_bits, e.g. a full Jolt pool or temp-allocator overflow),
// 6 backend unavailable. With both kinds of bits the engine error wins (3); the manifest records both groups.
// Every timing written by M3 is UNOFFICIAL: no measurement protocol (M7), no power-plan check (the plan is only read and
// recorded), no conformance stamp (M10). The manifest says so (timing_valid_for_headline = false, "unofficial").
#pragma once

#include "CoreTypes.h"
#include "Physics/PhysSceneStats.h"
#include "PhysicsTestScenario.h"

#include <filesystem>
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
		Contaminated = 4,        // M7
		Capacity = 5,
		BackendUnavailable = 6,
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
		uint32 InjectEngineErrorBits = 0;    // -InjectEngineErrorBits=0x200: test only, OR-ed into the run's engine error bits
		                                     // after the loop (CTest drives exit codes 3 / 5 through the exe); the manifest says so.
		                                     // With -Conformance: OR-ed into every physics-gate frame, so G12 fails (M6 round 1)
		bool bList = false;
		bool bDescribe = false;
		// -Conformance (M6, 02 14.3): the physics gate suite on this backend / variant / profile -> conformance.json in the
		// run directory; exit 0 when every gate passes or skips, 2 when one fails. -Gates=G1,G9 selects (default: all).
		bool bConformance = false;
		std::vector<std::string> Gates;
		// -SceneHashes=<path> (M6): writes / updates the golden scene hashes of every gate scene (scene_sha256, and
		// desc_sha256 of this build configuration per profile) at <path>; the other configuration's entries are kept.
		std::filesystem::path SceneHashesPath;

		// UE-style switches (FParse). False + OutError for a malformed value (e.g. -Threads=4x, -Frames=0).
		static bool FromCommandLine(const char* CommandLine, FBenchRunSpec& Out, std::string& OutError);
		// Only the -Param:Name=Value tokens (FromCommandLine uses it; the viewer too, which has its own -Frames meaning).
		// False + OutError for a malformed token.
		static bool ParseScenarioParams(const char* CommandLine, FScenarioParams& OutParams, std::string& OutError);
	};

	// One row of timings.csv per frame (02 12.3), kept in a pre-sized array: the timed loop never allocates.
	struct FFrameRecord
	{
		FFrameTiming Timing;
		double SimTime = 0.0;
		int32 AwakeBodies = -1;
	};

	class FTimingRecorder
	{
	public:
		void Reserve(int32 Frames) { Records.reserve(static_cast<size_t>(Frames)); }
		void Record(const FFrameTiming& Timing, double SimTime, int32 AwakeBodies) { Records.push_back(FFrameRecord{ Timing, SimTime, AwakeBodies }); }
		// frame, sim_time_s, num_steps, push_ns, setup_ns, start_ns, wait_ns, end_ns, step_ns, pull_ns, frame_ns, frame_qpc_ticks,
		// solver_ns, awake_bodies, contact_pairs, contact_points, islands, error_bits. A value the run did not measure is
		// empty (solver_ns: no phase timers), a counter it could not get is -1 (CountersEvery = 0, no listener).
		bool WriteCsv(const std::filesystem::path& Path) const;
		[[nodiscard]] const std::vector<FFrameRecord>& GetRecords() const { return Records; }

	private:
		std::vector<FFrameRecord> Records;
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
	bool LoadBenchConfig(const FBenchRunSpec& Spec, FResolvedPhysicsConfig& OutConfig, FBenchConfig& OutFiles, std::string& OutError);
	// <repo>/Config unless -ConfigDir= was given.
	[[nodiscard]] std::filesystem::path GetDefaultConfigDir();
	// <repo>/Saved/Results
	[[nodiscard]] std::filesystem::path GetDefaultResultsDir();

	class FBenchmarkRunner
	{
	public:
		// One run (02 12.2, M3 subset); writes the outputs into the run directory and returns the exit code.
		EBenchExit Run(const FBenchRunSpec& Spec);
		// -List: scenarios with their parameters and the registered backends.
		static EBenchExit List();
		// -Describe: the backend's info, capabilities, required keys and the effective settings of a scene built with the
		// resolved profile (the scenario's scene when -Scenario is given, else an empty scene), as JSON on stdout.
		static EBenchExit Describe(const FBenchRunSpec& Spec);
		// -Conformance (M6): the physics gates on the backend / variant / profile of Spec; writes conformance.json (schema
		// physicstest.conformance/1) into the run directory and prints its path. 0 = every gate passed or skipped.
		static EBenchExit Conformance(const FBenchRunSpec& Spec);
		// -SceneHashes=<path> (M6): the golden scene hashes of the gate scenes (and S00.FreeFall), built on Null.
		static EBenchExit WriteSceneHashes(const FBenchRunSpec& Spec);
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
