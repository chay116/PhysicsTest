# PT::Benchmark - the bench run (02_Architecture 4.5, 12, A.12). M3 minimal: FBenchRunSpec subset, the FixedStep loop,
# timings.csv / manifest.json / effective_settings.jsonl. M7 adds the host controls, recorders, preflight and exit.json.
# Benchmark -> Conformance -> BenchCore (no cycle, DL53). No renderer: the bench exe never imports d3d11 / dxgi.
pt_add_module(PtBenchmark STATIC ALIAS PT::Benchmark
	SOURCES
		Public/BenchmarkRunner.h
		Private/BenchConfig.cpp
		Private/BenchmarkRunner.cpp
	PUBLIC_DEPS PT::Conformance PT::Scenarios PT::BenchCore
	PRIVATE_DEPS PT::Json PT::BuildInfo)
# Default Config/ and Saved/Results/ locations (overridable with -ConfigDir= and -Out=).
target_compile_definitions(PtBenchmark PRIVATE "PT_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")
