// ConformanceStamp.h - the conformance stamp (02_Architecture 12.4, R9, A.12 FConformanceStampKey; 03_WorkPlan M10 task 4).
// [확장 M10]
// PhysicsTestBench -Conformance runs the gate suite on the binary's own backend code and writes, next to conformance.json,
// a stamp: Saved/Conformance/<stamp_key_sha256>.json (schema physicstest.stamp/1, Config/Schemas/stamp.schema.json). The key
// binds the result to everything that decides it:
//   bench_exe_sha256        the bench exe (static backends and the gate code are inside it)
//   backend_module_sha256   a DLL backend's file (Null DLL, Chaos); "" for a static backend
//   variant_sha256          the backend variant ini's content (not its name: an edited PhysX.UE4Compat.ini invalidates)
//   physics_sha256          the resolved physics profile
//   tolerances_sha256       Config/Golden/tolerances.json
//   gate_suite_version      Gates::GateSuiteVersion
//   config_overrides        (M10 addition, Progress M10 D-*) the -ini: / -ExecCmds switches the stamped configuration was
//                           resolved with, sorted: an engine key set on the command line changes neither physics_sha256
//                           nor variant_sha256, so without it a matrix target's -ini:[PhysicsTest.Backend.Bullet] would
//                           ride on the plain variant's stamp
// The key's hash is SHA-256 over its canonical JSON (keys sorted, no whitespace, UTF-8). A stamp is valid when the suite
// passed (no gate failed) and no verdict rests on an unsigned post-run registration (pending_sign_offs 0). The bench records
// the stamp it finds for its own run (manifest conformance_stamp); the runner (Tools/ptbench) re-derives every component from
// the files before it starts a process and refuses an unstamped or stale cell.
#pragma once

#include "ConformanceGate.h"

#include <filesystem>
#include <string>
#include <vector>

namespace PhysicsTest
{
	struct FConformanceStampKey
	{
		std::string BenchExeSha256;
		std::string BackendModuleSha256;           // "" = a static backend (part of the exe)
		std::string VariantSha256;                 // "" = no variant
		std::string PhysicsSha256;
		std::string TolerancesSha256;
		int32 GateSuiteVersion = 0;
		std::vector<std::string> ConfigOverrides;  // sorted (ExtractConfigOverrides)

		// {"backend_module_sha256":..,"bench_exe_sha256":..,"config_overrides":[..],"gate_suite_version":N,"physics_sha256":..,
		//  "tolerances_sha256":..,"variant_sha256":..} - sorted keys, no whitespace (Tools/ptbench/stamps.py builds the same bytes).
		[[nodiscard]] std::string ToCanonicalJson() const;
		[[nodiscard]] std::string ComputeSha256() const;
		[[nodiscard]] std::filesystem::path ToPath(const std::filesystem::path& StampDir) const;   // <dir>/<sha256>.json
	};

	// The -ini: and -ExecCmds= switches of a command line (FParse::Token), double quotes removed, sorted. The runner computes the
	// same list from a matrix cell's switches (Tools/ptbench/stamps.py config_overrides).
	[[nodiscard]] std::vector<std::string> ExtractConfigOverrides(const std::string& CommandLine);

	// One input file of the stamped configuration: the runner re-hashes it (raw bytes) before every session.
	struct FStampInput
	{
		std::string Role;                          // base_ini, profile_ini, variant_ini, tolerances
		std::filesystem::path Path;
	};

	struct FStampGate
	{
		std::string Id;
		EGateResult Result = EGateResult::Pass;
		std::vector<std::string> Failures;
		std::string Reason;                        // a Skip's reason / the first failure
	};

	struct FStampDocument
	{
		FConformanceStampKey Key;
		std::string Backend;                       // registry name
		std::string VariantRequested;              // -BackendProfile as given ("" = the bench's default resolution)
		std::string Variant;                       // the resolved variant
		std::string Profile;
		std::string Preset;
		std::string FlagProfile;
		std::string Configuration;
		std::string GitSha;
		bool bGitDirty = false;
		std::filesystem::path ExePath;
		std::string ModuleKind;                    // "static" or "dll"
		std::filesystem::path ModulePath;          // DLL backends
		std::filesystem::path ConfigDir;
		std::vector<FStampInput> Inputs;
		std::vector<FStampGate> Gates;
		int32 PendingSignOffs = 0;
		std::filesystem::path ConformanceDir;      // the run directory with conformance.json
		std::string CommandLine;
	};

	// Writes <StampDir>/<key sha256>.json atomically (temporary file + rename); the input files are hashed here. False +
	// OutError when it cannot be written.
	bool WriteConformanceStamp(const FStampDocument& Document, const std::filesystem::path& StampDir, std::filesystem::path& OutPath, std::string& OutError);

	// What the bench records about the stamp of its own run (manifest conformance_stamp).
	struct FStampCheck
	{
		std::string KeySha256;
		std::filesystem::path Path;
		bool bFound = false;
		bool bValid = false;
		std::string Reason;                        // why not valid ("" when valid)
		std::string StampUtc;
	};
	[[nodiscard]] FStampCheck CheckConformanceStamp(const FConformanceStampKey& Key, const std::filesystem::path& StampDir);
}
