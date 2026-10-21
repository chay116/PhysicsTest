# PhysicsTestBench.exe - the minimal bench exe of M3 (02_Architecture 4.5, 12; 03_WorkPlan M3). Links PT::Benchmark, the
# generated backend registry and every scenario (pt_link_scenarios: /WHOLEARCHIVE). Never a renderer: the CTest probe
# PhysicsTest.Probe.BenchNoRenderer checks that the exe imports neither d3d11.dll nor dxgi.dll.
pt_add_program(PhysicsTestBench
	SOURCES Private/PhysicsTestBenchMain.cpp
	DEPS PT::Core PT::Benchmark PT::BackendRegistry PT::BuildInfo)
pt_link_scenarios(PhysicsTestBench)
# The linker map feeds PhysicsTest.Probe.AdapterComdats: the timing exe must not run an AVX2 adapter copy of a COMDAT the
# harness shares (docs/Abi.md 5.13).
target_link_options(PhysicsTestBench PRIVATE "/MAP:$<TARGET_FILE_DIR:PhysicsTestBench>/PhysicsTestBench.map")

# Smoke: -List succeeds and names the M3 scenes (the registry is not empty, WHOLE_ARCHIVE worked; the scenario unit tests
# check the rest).
add_test(NAME PhysicsTest.Bench.List COMMAND PhysicsTestBench -List)
set_tests_properties(PhysicsTest.Bench.List PROPERTIES LABELS "bench" PASS_REGULAR_EXPRESSION "M3[.]BoxPile")

# 03 M3: M1.SingleBoxDrop a/b and M3.BoxPile 1k/5k under UEParity and Normalized write the three outputs; Null and the SDK
# backend agree on desc_sha256; shape.wrapping is 0. 30-frame runs (the 600-frame runs are a manual step, Progress.md M3).
if(PT_WITH_JOLT)
	set(_pt_bench_backend Jolt)
else()
	set(_pt_bench_backend "")
endif()
pt_add_script_test(PhysicsTest.Bench.M3Matrix
	SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunBenchMatrix.cmake"
	ARGS "-DPT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_OUT_DIR=${CMAKE_BINARY_DIR}/_bench/M3Matrix" "-DPT_BACKEND=${_pt_bench_backend}"
		-DPT_FRAMES=30
	LABELS "bench")

# Exit codes (02 12.2): 6 no backend, 2 an invalid command line / scenario / parameter / engine option (the last writes
# manifest.json with status preflight_failed) or HashEvery / CountersEvery > 0 (M7 recorder), 5 a capacity overflow (the
# Jolt temp block forced to 1 MiB overflows on M3.BoxPile 1k: the heap fallback becomes engine error bit 0x100), 3 an
# engine error bit outside the backend's errors.capacity_bits (driven through the exe by the test-only
# -InjectEngineErrorBits; it also wins over a capacity bit of the same run). An ABI failure after the preflight stops the
# process through Fatal (3), whose exit code PhysicsTest.Probe.NegativeScaleFatal checks.
set(_pt_exit_script "${CMAKE_SOURCE_DIR}/Source/Tests/Probes/CheckFatalExit.cmake")
if(PT_WITH_JOLT)
	set(_pt_bench_option_backend Jolt)
else()
	set(_pt_bench_option_backend Null)
endif()
pt_add_script_test(PhysicsTest.Bench.ExitBackendUnavailable SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=6 "-DPT_EXPECT_REGEX=is not available" "-DPT_ARGS=-Backend=NoSuchEngine|-Scenario=S00.FreeFall"
	LABELS "bench")
pt_add_script_test(PhysicsTest.Bench.ExitBadCommandLine SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=2 "-DPT_EXPECT_REGEX=-Threads=4x must be an integer" "-DPT_ARGS=-Backend=Null|-Scenario=S00.FreeFall|-Threads=4x"
	LABELS "bench")
pt_add_script_test(PhysicsTest.Bench.ExitUnknownScenario SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=2 "-DPT_EXPECT_REGEX=unknown scenario 'NoSuch.Scene'" "-DPT_ARGS=-Backend=Null|-Scenario=NoSuch.Scene"
	LABELS "bench")
pt_add_script_test(PhysicsTest.Bench.ExitUnknownParam SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=2 "-DPT_EXPECT_REGEX=has no parameter 'Count'" "-DPT_ARGS=-Backend=Null|-Scenario=M3.BoxPile|-Param:Count=3"
	LABELS "bench")
pt_add_script_test(PhysicsTest.Bench.ExitBadSeed SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=2 "-DPT_EXPECT_REGEX=Seed=abc is not a decimal 32-bit integer" "-DPT_ARGS=-Backend=Null|-Scenario=M3.BoxPile|-Param:Seed=abc"
	LABELS "bench")
# An engine key the backend does not know is a configuration error: exit 2 and a manifest (status preflight_failed), not
# FPhysScene's Fatal (3) without output.
pt_add_script_test(PhysicsTest.Bench.ExitBadEngineOption SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=2 "-DPT_EXPECT_REGEX=rejects the variant's engine options"
		"-DPT_ARGS=-Backend=${_pt_bench_option_backend}|-Scenario=S00.FreeFall|-ini:PhysicsTest:[PhysicsTest.Backend.${_pt_bench_option_backend}]:NoSuchKey=1|-Out=${CMAKE_BINARY_DIR}/_bench/ExitBadEngineOption"
		"-DPT_EXPECT_FILE=${CMAKE_BINARY_DIR}/_bench/ExitBadEngineOption/manifest.json" "-DPT_EXPECT_FILE_REGEX=.status.: .preflight_failed.*.preflight_error.: .engine options"
	LABELS "bench")
# A missing required effective key is a preflight failure: exit 2, Prepare and the frame loop are skipped (no frame in
# timings.csv), manifest.json says preflight_failed and frames_run 0.
pt_add_script_test(PhysicsTest.Bench.ExitPreflightMissingKey SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=2 "-DPT_EXPECT_REGEX=Prepare and the frame loop are skipped"
		"-DPT_ARGS=-Backend=Null|-Scenario=S00.FreeFall|-RequireEffectiveKey=no.such.key|-Out=${CMAKE_BINARY_DIR}/_bench/ExitPreflightMissingKey"
		"-DPT_EXPECT_FILE=${CMAKE_BINARY_DIR}/_bench/ExitPreflightMissingKey/manifest.json"
		"-DPT_EXPECT_FILE_REGEX=.status.: .preflight_failed.*.missing_effective_keys.: [[][^]]*no[.]such[.]key.*.frames_run.: 0,"
	LABELS "bench")
# Per-frame hashes / counters are the M7 recorder: the M3 runner refuses them instead of writing a manifest that claims them.
pt_add_script_test(PhysicsTest.Bench.ExitHashEvery SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=2 "-DPT_EXPECT_REGEX=need the M7 recorder"
		"-DPT_ARGS=-Backend=Null|-Scenario=S00.FreeFall|-ini:PhysicsTest:[/Script/PhysicsTest.PhysicsTestSettings]:HashEvery=1"
	LABELS "bench")
# An engine error bit the backend does not declare as a capacity bit: exit 3, manifest status engine_error.
pt_add_script_test(PhysicsTest.Bench.ExitEngineError SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=3 "-DPT_EXPECT_REGEX=status engine_error"
		"-DPT_ARGS=-Backend=Null|-Scenario=S00.FreeFall|-Frames=2|-InjectEngineErrorBits=0x200|-Out=${CMAKE_BINARY_DIR}/_bench/ExitEngineError"
		"-DPT_EXPECT_FILE=${CMAKE_BINARY_DIR}/_bench/ExitEngineError/manifest.json"
		"-DPT_EXPECT_FILE_REGEX=.status.: .engine_error.*.injected_engine_error_bits.: 512.*.capacity_error_bits.: 0,.*.other_error_bits.: 512"
	LABELS "bench")
if(PT_WITH_JOLT)
	pt_add_script_test(PhysicsTest.Bench.ExitCapacity SCRIPT "${_pt_exit_script}"
		ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=5 "-DPT_EXPECT_REGEX=status capacity_exceeded"
			"-DPT_ARGS=-Backend=Jolt|-Scenario=M3.BoxPile|-Frames=5|-ini:PhysicsTest:[PhysicsTest.Backend.Jolt]:TempAllocatorBytes=1048576|-Out=${CMAKE_BINARY_DIR}/_bench/ExitCapacity"
			"-DPT_EXPECT_FILE=${CMAKE_BINARY_DIR}/_bench/ExitCapacity/manifest.json"
			"-DPT_EXPECT_FILE_REGEX=.status.: .capacity_exceeded.*.capacity_error_bits_declared.: 263,.*.capacity_error_bits.: [1-9][0-9]*,.*.other_error_bits.: 0"
		LABELS "bench")
	# Both kinds in one run: the engine error wins (3) and the manifest keeps the capacity bits.
	pt_add_script_test(PhysicsTest.Bench.ExitEngineErrorOverCapacity SCRIPT "${_pt_exit_script}"
		ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=3 "-DPT_EXPECT_REGEX=status engine_error"
			"-DPT_ARGS=-Backend=Jolt|-Scenario=M3.BoxPile|-Frames=5|-ini:PhysicsTest:[PhysicsTest.Backend.Jolt]:TempAllocatorBytes=1048576|-InjectEngineErrorBits=0x200|-Out=${CMAKE_BINARY_DIR}/_bench/ExitEngineErrorOverCapacity"
			"-DPT_EXPECT_FILE=${CMAKE_BINARY_DIR}/_bench/ExitEngineErrorOverCapacity/manifest.json"
			"-DPT_EXPECT_FILE_REGEX=.status.: .engine_error.*.capacity_error_bits.: [1-9][0-9]*,.*.other_error_bits.: 512"
		LABELS "bench")
endif()
