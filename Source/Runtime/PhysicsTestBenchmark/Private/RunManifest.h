// RunManifest.h - FRunManifest: manifest.json of one bench run (02_Architecture 12.3, A.12, docs/ResultsSchema.md). A private
// header of PT::Benchmark (M7 fix round 0): only BenchmarkRunner.cpp writes manifests, so nlohmann/json stays a private
// dependency of the module.
// [MVP M3 basic fields (schema physicstest.run/1, written by the runner itself); 확장 M7 the full set (physicstest.run/2) and
// Config/Schemas/manifest.schema.json; 확장 M10 the comparability key and the conformance stamp]
// The runner fills Json group by group (status, harness, build, backend, profile, effective, scene, run, threads, platform,
// clock, order, phases_ns, memory, alloc, summary, determinism, gates, outputs); Write pretty-prints it.
#pragma once

#include "Misc/PtThirdPartyIncludesStart.h"
#include <nlohmann/json.hpp>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <filesystem>
#include <string>

namespace PhysicsTest
{
	struct FRunManifest
	{
		// run/2 (M7): run/1 manifests (M3 - M6) lack the host, clock, order, alloc and gates groups and always say
		// timing_valid_for_headline = false; Config/Schemas/manifest.schema.json describes run/2.
		static constexpr const char* SchemaId = "physicstest.run/2";

		nlohmann::ordered_json Json;

		// Pretty-printed (2 spaces) and flushed. False when the file cannot be written.
		bool Write(const std::filesystem::path& Path) const;
		// [확장 M10] The comparability key (02 13): SHA-256 of the canonical JSON (keys sorted, no whitespace) of
		//   {"schema": "physicstest.comparability/1", scene_sha256, desc_sha256, physics_sha256, mass_rule, flag_profile,
		//    tick_mode, dt_bits (the IEEE-754 bits of run.dt_s as 16 hex digits), T, thread_series, sync_policy, mode,
		//    instrumentation (sorted), harness_commit (harness.git_sha, "+dirty" when harness.dirty)}
		// read from Json's groups. thread_series is an M10 addition (TimingContract 7: runs of another mask shape are not
		// compared). Tools/ptbench/analysis/comparability.py recomputes it from the same fields; validate rejects a manifest
		// whose key does not match them. Frames are inside desc_sha256 (the dt sequence), the windows inside scene_sha256.
		[[nodiscard]] std::string ComputeComparabilityKey() const;
	};
}
