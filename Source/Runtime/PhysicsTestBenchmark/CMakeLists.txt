# PT::Benchmark - the bench run (02_Architecture 4.5, 12, A.12). M3 minimal: FBenchRunSpec subset, the FixedStep loop,
# timings.csv / manifest.json / effective_settings.jsonl. M6: -Conformance. M7: the full run order with the host controls,
# the recorders (hashes.csv, states.ptst, contacts.ptcn), FRunManifest (schema physicstest.run/2,
# Config/Schemas/manifest.schema.json), the preflight (G0, G9, G10a), exit.json and the multi-process gates G8 / G11.
# Benchmark -> Conformance -> BenchCore (no cycle, DL53). No renderer: the bench exe never imports d3d11 / dxgi.
# PT::Json is PRIVATE (M7 fix round 0): RunManifest.h, which holds an nlohmann::ordered_json, is a private header of this
# module - only BenchmarkRunner.cpp writes manifests (A.12 sketched it public).
pt_add_module(PtBenchmark STATIC ALIAS PT::Benchmark
	SOURCES
		Public/BenchmarkRunner.h
		Private/RunManifest.h
		Private/BenchConfig.cpp
		Private/BenchmarkRunner.cpp
		Private/BenchMultiRun.cpp
		Private/BenchPreflight.cpp
		Private/BenchPreflight.h
		Private/BenchRecorders.cpp
		Public/BenchRecorders.h
	PUBLIC_DEPS PT::Conformance PT::Scenarios PT::BenchCore
	PRIVATE_DEPS PT::BuildInfo PT::Json)
# Default Config/ and Saved/Results/ locations (overridable with -ConfigDir= and -Out=).
target_compile_definitions(PtBenchmark PRIVATE "PT_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
