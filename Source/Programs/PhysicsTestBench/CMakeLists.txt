# PhysicsTestBench.exe - the bench exe (02_Architecture 4.5, 12; 03_WorkPlan M3 minimal, M6 -Conformance, M7 full). Links PT::Benchmark, the
# generated backend registry and every scenario (pt_link_scenarios: /WHOLEARCHIVE). Never a renderer: the CTest probe
# PhysicsTest.Probe.BenchNoRenderer checks that the exe imports neither d3d11.dll nor dxgi.dll.
# AllocationHook.cpp replaces the global operator new / delete of this exe (M7: the timed loop's allocation count).
pt_add_program(PhysicsTestBench
	SOURCES Private/PhysicsTestBenchMain.cpp Private/AllocationHook.cpp
	DEPS PT::Core PT::Benchmark PT::BackendRegistry PT::BuildInfo)
pt_link_scenarios(PhysicsTestBench)
# The linker map feeds PhysicsTest.Probe.AdapterComdats: the timing exe must not run an AVX2 adapter copy of a COMDAT the
# harness shares (docs/Abi.md 5.13).
target_link_options(PhysicsTestBench PRIVATE "/MAP:$<TARGET_FILE_DIR:PhysicsTestBench>/PhysicsTestBench.map")
# M10: the backend catalog next to the exe (static vs DLL backends; Tools/ptbench/stamps.py hashes a DLL backend's file
# before it starts a run, 02 12.4).
get_property(_pt_backend_catalog GLOBAL PROPERTY PT_BACKEND_CATALOG_JSON)
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:PhysicsTestBench>/PhysicsTestBench.backends.json" CONTENT "${_pt_backend_catalog}")

# Smoke: -List succeeds and names the M3 scenes (the registry is not empty, WHOLE_ARCHIVE worked; the scenario unit tests
# check the rest).
add_test(NAME PhysicsTest.Bench.List COMMAND PhysicsTestBench -List)
set_tests_properties(PhysicsTest.Bench.List PROPERTIES LABELS "bench" PASS_REGULAR_EXPRESSION "M3[.]BoxPile")

# 03 M3 / M5: M1.SingleBoxDrop a/b and M3.BoxPile 1k/5k under UEParity and Normalized write the three outputs; Null and
# every SDK backend and PhysX variant agree on desc_sha256 (M5: "jolt, bullet, physx 변형 4종에서 씬·프로필마다 같다");
# shape.wrapping is 0. 30-frame runs (the 600-frame runs are a manual step, Progress.md M3 / M5). Every run is a fresh
# process, so the PhysX SdkDefault runs (their own PxTolerancesScale) never meet a UE4Compat PxPhysics.
set(_pt_bench_backends "")
if(PT_WITH_JOLT)
	list(APPEND _pt_bench_backends Jolt)
endif()
if(PT_WITH_BULLET)
	list(APPEND _pt_bench_backends Bullet)
endif()
if(PT_WITH_PHYSX)
	list(APPEND _pt_bench_backends PhysX/UE4Compat PhysX/UE4Compat-TGS PhysX/UE4Compat-Stab PhysX/SdkDefault)
endif()
string(REPLACE ";" "," _pt_bench_backends "${_pt_bench_backends}")
pt_add_script_test(PhysicsTest.Bench.M3Matrix
	SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunBenchMatrix.cmake"
	ARGS "-DPT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_OUT_DIR=${CMAKE_BINARY_DIR}/_bench/M3Matrix" "-DPT_BACKEND=${_pt_bench_backends}"
		-DPT_FRAMES=30
	LABELS "bench")

# 03 M6: PhysicsTestBench -Conformance writes conformance.json for every backend x variant x physics profile (one fresh
# process each), every gate passing or skipped; -Describe / -List stdout is pure JSON (lead (g)).
pt_add_script_test(PhysicsTest.Bench.Conformance
	SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunConformanceMatrix.cmake"
	ARGS "-DPT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_OUT_DIR=${CMAKE_BINARY_DIR}/_bench/Conformance" "-DPT_BACKEND=${_pt_bench_backends}"
	LABELS "bench")
# G12 can fail (M6 round 1): the test-only -InjectEngineErrorBits is OR-ed into every gate frame's engine error bits, so
# G12 reports Fail, the bench exits 2 and conformance.json records the injected bits.
pt_add_script_test(PhysicsTest.Bench.ConformanceG12Injected SCRIPT "${CMAKE_SOURCE_DIR}/Source/Tests/Probes/CheckFatalExit.cmake"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=2 "-DPT_EXPECT_REGEX=G12 Null / UEParity: Fail.*FAIL engine error bits 0x1 over"
		"-DPT_ARGS=-Conformance|-Backend=Null|-Profile=UEParity|-Gates=G12|-InjectEngineErrorBits=0x1|-Out=${CMAKE_BINARY_DIR}/_bench/ConformanceG12Injected"
		"-DPT_EXPECT_FILE=${CMAKE_BINARY_DIR}/_bench/ConformanceG12Injected/conformance.json"
		"-DPT_EXPECT_FILE_REGEX=.result.: .fail.,.*.injected_engine_error_bits.: 1,.*.id.: .G12.,.*.result.: .Fail."
	LABELS "bench")
# Exit codes (02 12.2, M7): every path leaves exit.json in the run directory with the code it intends (the M8 runner trusts
# it over the process exit code). 6 no backend; 2 an invalid command line / scenario / parameter (category usage), an engine
# option the backend rejects or a failed preflight gate (category preflight, manifest.json status preflight_failed); 5 a
# capacity overflow (the Jolt temp block forced to 1 MiB overflows on M3.BoxPile 1k: the heap fallback becomes engine error
# bit 0x100); 3 an engine error bit outside the backend's errors.capacity_bits (the test-only -InjectEngineErrorBits; it also
# wins over a capacity bit of the same run) and a Fatal (the hook writes exit.json first: -InjectFatal); 4 a contaminated
# host (PhysicsTest.Bench.ExitContaminated). Runs that reach the loop pass -HostCheck=record -PreRoll=0: parallel CTest
# processes share CPU 0 (exit 4 is its own test) and timing is not what these tests check.
set(_pt_exit_script "${CMAKE_SOURCE_DIR}/Source/Tests/Probes/CheckFatalExit.cmake")
set(_pt_bench_dir "${CMAKE_BINARY_DIR}/_bench")
set(_pt_ctest_run_args "-HostCheck=record|-PreRoll=0")
# The SDK backend of the option / preflight / schema / stamp tests: the first one this build has (M10: the single-SDK presets
# test-physx and test-bullet have no Jolt). Null takes no engine options, so a build without any SDK cannot run them.
if(PT_WITH_JOLT)
	set(_pt_bench_option_backend Jolt)
elseif(PT_WITH_PHYSX)
	set(_pt_bench_option_backend PhysX)
elseif(PT_WITH_BULLET)
	set(_pt_bench_option_backend Bullet)
else()
	set(_pt_bench_option_backend Null)
endif()

# pt_add_bench_exit_test(<Name> <rc> <stdout regex> <exit.json status> <args...>): the bench with -Out=_bench/<Name> must exit
# <rc>, print <regex> and leave exit.json with "exit_code": <rc> and "status": "<status>" (EXIT_REGEX: more of exit.json);
# MANIFEST_REGEX checks manifest.json, TEARDOWN_REGEX teardown.json.
function(pt_add_bench_exit_test Name Rc Regex Status)
	cmake_parse_arguments(PARSE_ARGV 4 Arg "" "MANIFEST_REGEX;FORBID_REGEX;EXIT_REGEX;TEARDOWN_REGEX" "ARGS;ENVIRONMENT")
	set(_dir "${_pt_bench_dir}/${Name}")
	string(REPLACE ";" "|" _args "${Arg_ARGS}")
	set(_exit_regex ".exit_code.: ${Rc},.*.status.: .${Status}.")
	if(Arg_EXIT_REGEX)
		string(APPEND _exit_regex ".*${Arg_EXIT_REGEX}")
	endif()
	set(_script_args "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_EXPECT_RC=${Rc}" "-DPT_EXPECT_REGEX=${Regex}" "-DPT_ARGS=${_args}|-Out=${_dir}"
		"-DPT_EXPECT_FILE=${_dir}/exit.json" "-DPT_EXPECT_FILE_REGEX=${_exit_regex}")
	if(Arg_MANIFEST_REGEX)
		list(APPEND _script_args "-DPT_EXPECT_FILE2=${_dir}/manifest.json" "-DPT_EXPECT_FILE2_REGEX=${Arg_MANIFEST_REGEX}")
	endif()
	if(Arg_TEARDOWN_REGEX)
		list(APPEND _script_args "-DPT_EXPECT_FILE3=${_dir}/teardown.json" "-DPT_EXPECT_FILE3_REGEX=${Arg_TEARDOWN_REGEX}")
	endif()
	if(Arg_FORBID_REGEX)
		list(APPEND _script_args "-DPT_FORBID_REGEX=${Arg_FORBID_REGEX}")
	endif()
	pt_add_script_test(PhysicsTest.Bench.${Name} SCRIPT "${_pt_exit_script}" ARGS ${_script_args} LABELS "bench")
	if(Arg_ENVIRONMENT)
		set_tests_properties(PhysicsTest.Bench.${Name} PROPERTIES ENVIRONMENT "${Arg_ENVIRONMENT}")
	endif()
endfunction()

# S08.StartAsleep means something only with sleeping allowed: the bench refuses it under Normalized (exit 2).
pt_add_bench_exit_test(ExitSceneNeedsSleeping 2 "needs a profile with sleeping allowed" usage_error
	ARGS -Backend=Null -Scenario=S08.StartAsleep -Profile=Normalized)
pt_add_bench_exit_test(ExitBackendUnavailable 6 "is not available" backend_unavailable ARGS -Backend=NoSuchEngine -Scenario=S00.FreeFall)
# A malformed switch is found before the run starts; exit.json still goes to -Out (main writes it, category usage).
pt_add_bench_exit_test(ExitBadCommandLine 2 "-Threads=4x must be an integer" usage_error ARGS -Backend=Null -Scenario=S00.FreeFall -Threads=4x)
pt_add_bench_exit_test(ExitUnknownScenario 2 "unknown scenario 'NoSuch.Scene'" usage_error ARGS -Backend=Null -Scenario=NoSuch.Scene)
pt_add_bench_exit_test(ExitUnknownParam 2 "has no parameter 'Count'" usage_error ARGS -Backend=Null -Scenario=M3.BoxPile -Param:Count=3)
pt_add_bench_exit_test(ExitBadSeed 2 "Seed=abc is not a decimal 32-bit integer" usage_error ARGS -Backend=Null -Scenario=M3.BoxPile -Param:Seed=abc)
# An engine key the backend does not know is a configuration error: exit 2 and a manifest (status preflight_failed), not
# FPhysScene's Fatal (3) without output. The manifest has every group (no scene: scene.built false, the scene-derived
# fields null; M7 fix round 0) and the teardown still runs.
pt_add_bench_exit_test(ExitBadEngineOption 2 "rejects the variant's engine options" preflight_failed
	ARGS -Backend=${_pt_bench_option_backend} -Scenario=S00.FreeFall "-ini:PhysicsTest:[PhysicsTest.Backend.${_pt_bench_option_backend}]:NoSuchKey=1"
	MANIFEST_REGEX ".status.: .preflight_failed.*.preflight_error.: .engine options.*.built.: false,.*.desc_sha256.: null.*.platform.: [{].*.alloc.: [{]"
	TEARDOWN_REGEX ".term_game_phys.: .PT_OK.")
# A missing required effective key is a preflight failure: exit 2, Prepare and the frame loop are skipped (no frame in
# timings.csv), manifest.json says preflight_failed and frames_run 0.
pt_add_bench_exit_test(ExitPreflightMissingKey 2 "Prepare and the frame loop are skipped" preflight_failed
	ARGS -Backend=Null -Scenario=S00.FreeFall -RequireEffectiveKey=no.such.key
	MANIFEST_REGEX ".status.: .preflight_failed.*.missing_effective_keys.: [[][^]]*no[.]such[.]key.*.frames_run.: 0,")
# An engine error bit the backend does not declare as a capacity bit: exit 3, manifest status engine_error.
pt_add_bench_exit_test(ExitEngineError 3 "status engine_error" engine_error
	ARGS -Backend=Null -Scenario=S00.FreeFall -Frames=2 -InjectEngineErrorBits=0x200 -HostCheck=record -PreRoll=0
	MANIFEST_REGEX ".status.: .engine_error.*.test_injections.: [{][^}]*.engine_error_bits.: 512.*.capacity_error_bits.: 0,.*.other_error_bits.: 512")
# M7: a Fatal after the preflight (-InjectFatal) ends the process with 3, and the Fatal hook has written exit.json first.
pt_add_bench_exit_test(ExitFatal 3 "Fatal: test-only -InjectFatal" fatal ARGS -Backend=Null -Scenario=S00.FreeFall -Frames=2 -InjectFatal -HostCheck=record -PreRoll=0)
# M7 fix round 0: a Fatal in the teardown, after exit.json, never rewrites it (DL52, docs/TimingContract.md 1): teardown.json
# records the Fatal and the process ends with the run's own code (0) through TerminateProcess.
pt_add_bench_exit_test(ExitTeardownFatal 0 "TerminateProcess[(]GetCurrentProcess[(][)], 0[)]: a Fatal during the teardown" ok
	ARGS -Backend=Null -Scenario=S00.FreeFall -Frames=2 -InjectTeardownFatal ${_pt_ctest_run_args}
	MANIFEST_REGEX ".status.: .ok."
	TEARDOWN_REGEX ".fatal.: .LogPhysicsBench: Fatal: test-only -InjectTeardownFatal.*.terminate_process.: true")
# M7 fix round 0: -Conformance leaves exit.json also when it stops before any gate (here: no such backend).
pt_add_bench_exit_test(ConformanceBackendUnavailable 6 "is not available in this build" backend_unavailable ARGS -Conformance -Backend=NoSuchEngine)
if(PT_WITH_JOLT)
	pt_add_bench_exit_test(ExitCapacity 5 "status capacity_exceeded" capacity_exceeded
		ARGS -Backend=Jolt -Scenario=M3.BoxPile -Frames=5 "-ini:PhysicsTest:[PhysicsTest.Backend.Jolt]:TempAllocatorBytes=1048576" -HostCheck=record -PreRoll=0
		MANIFEST_REGEX ".status.: .capacity_exceeded.*.capacity_error_bits_declared.: 263,.*.capacity_error_bits.: [1-9][0-9]*,.*.other_error_bits.: 0")
	# Both kinds in one run: the engine error wins (3) and the manifest keeps the capacity bits.
	pt_add_bench_exit_test(ExitEngineErrorOverCapacity 3 "status engine_error" engine_error
		ARGS -Backend=Jolt -Scenario=M3.BoxPile -Frames=5 "-ini:PhysicsTest:[PhysicsTest.Backend.Jolt]:TempAllocatorBytes=1048576" -InjectEngineErrorBits=0x200
			-HostCheck=record -PreRoll=0
		MANIFEST_REGEX ".status.: .engine_error.*.capacity_error_bits.: [1-9][0-9]*,.*.other_error_bits.: 512")
endif()
if(PT_WITH_BULLET)
	# M5: a Bullet manifold pool below the pile's needs falls back to the heap: capacity bit 0x1 (errors.capacity_bits 0x3:
	# manifold 0x1, collision algorithm 0x2) -> exit 5.
	pt_add_bench_exit_test(ExitCapacityBullet 5 "status capacity_exceeded" capacity_exceeded
		ARGS -Backend=Bullet -Scenario=M3.BoxPile -Frames=60 "-ini:PhysicsTest:[PhysicsTest.Backend.Bullet]:ManifoldPoolSize=64" -HostCheck=record -PreRoll=0
		MANIFEST_REGEX ".status.: .capacity_exceeded.*.capacity_error_bits_declared.: 3,.*.capacity_error_bits.: [1-9][0-9]*,.*.other_error_bits.: 0")
	# 02 9.6: Bullet runs T = 1 only (no PT_CAP_MULTITHREADED); T = 2 is a usage error.
	pt_add_bench_exit_test(ExitBulletThreads 2 "runs single-threaded only" usage_error ARGS -Backend=Bullet -Scenario=M3.BoxPile -Frames=2 -Threads=2)
endif()
# M7: exit 4 - another process's load on the run's CPU (a busy helper pinned to CPU 0 next to a bench run on CPU 0).
# The helper spins 10 s (M8 fix round 1; 4 s before): it must cover the bench's whole measured loop, and twice in full -j6
# runs (dbg, test) the bench reached its loop only after a 4 s helper had ended (exit 0, 'host other 2.63%').
pt_add_script_test(PhysicsTest.Bench.ExitContaminated SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunContaminated.cmake"
	ARGS "-DPT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_OUT_DIR=${_pt_bench_dir}/ExitContaminated" -DPT_HELPER_SECONDS=10
	LABELS "bench")
# The busy helper and the contaminated run must not share CPU 0 with other CTest processes in a parallel run.
set_tests_properties(PhysicsTest.Bench.ExitContaminated PROPERTIES RUN_SERIAL ON)
# M7 fix round 0 (TimingContract 5.1): the load on CPU 0's SMT sibling counts too - the bench's core is shared.
pt_add_script_test(PhysicsTest.Bench.ExitContaminatedSibling SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunContaminated.cmake"
	ARGS "-DPT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_OUT_DIR=${_pt_bench_dir}/ExitContaminatedSibling" -DPT_HELPER_SECONDS=10 -DPT_TARGET=sibling
	LABELS "bench")
set_tests_properties(PhysicsTest.Bench.ExitContaminatedSibling PROPERTIES RUN_SERIAL ON)

# M7 headline eligibility (03 M7): a tier-0 bench run is headline-valid only when every protocol condition holds; an
# instrumented run is never refused (G10a) but says timing_valid_for_headline false with the tier as the reason; a viz run
# likewise. The test preset's default run (no injection, pre-roll, record-only host check) is valid except for the host check.
pt_add_bench_exit_test(InstrumentCounters 0 "status ok" ok
	ARGS -Backend=Null -Scenario=S00.FreeFall -Frames=4 -Instrument=counters ${_pt_ctest_run_args}
	MANIFEST_REGEX ".timing_valid_for_headline.: false,.*.timing_invalid_reasons.: [[][^]]*instrumentation tier counters.*.instrumentation.: [[][^]]*.counters.")
# M7 fix round 0: the phases tier reads the backend's phase timers every frame (timings.csv solver_ns, 02 9.7) - the Null DLL's
# test double declares PT_CAP_PHASE_TIMERS with PT_NULL_DLL_PHASE_TIMERS=1; the counter columns stay off (CountersEvery 0).
pt_add_bench_exit_test(InstrumentPhases 0 "status ok" ok
	ARGS -Backend=NullDll -Scenario=S00.FreeFall -Frames=4 -Instrument=phases ${_pt_ctest_run_args}
	MANIFEST_REGEX ".timing_invalid_reasons.: [[][^]]*instrumentation tier phases.*.counters_every.: 0,.*.phases.: [{][^}]*.frames.: 4,[^}]*.null[.]steps."
	ENVIRONMENT PT_NULL_DLL_PHASE_TIMERS=1)
pt_add_bench_exit_test(HeadlineIneligibleViz 0 "status ok" ok
	ARGS -Backend=Null -Scenario=S00.FreeFall -Frames=2 -RunMode=Viz ${_pt_ctest_run_args}
	MANIFEST_REGEX ".timing_valid_for_headline.: false,.*.timing_invalid_reasons.: [[][^]]*G10a: run mode Viz")
# Per-frame recorders (M7): HashEvery=1 writes every frame's hashes, StatesEvery=10 states.ptst + states.dtype.json.
pt_add_script_test(PhysicsTest.Bench.Recorders SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=0 "-DPT_EXPECT_REGEX=status ok"
		"-DPT_ARGS=-Backend=Null|-Scenario=M3.BoxPile|-Frames=20|-HashEvery=1|-StatesEvery=10|${_pt_ctest_run_args}|-Out=${_pt_bench_dir}/Recorders"
		"-DPT_EXPECT_FILE=${_pt_bench_dir}/Recorders/hashes.csv" "-DPT_EXPECT_FILE_REGEX=^frame,h_native,h_quantized,h_state.0,0x[0-9a-f]+,0x[0-9a-f]+,0x[0-9a-f]+.*19,0x[0-9a-f]+,0x[0-9a-f]+,0x[0-9a-f]+"
		"-DPT_EXPECT_FILE2=${_pt_bench_dir}/Recorders/states.dtype.json" "-DPT_EXPECT_FILE2_REGEX=.body_count.: 1001,.*.frame_count.: 2,"
		"-DPT_EXPECT_FILE3=${_pt_bench_dir}/Recorders/manifest.json" "-DPT_EXPECT_FILE3_REGEX=.states.: [{][^}]*.frames.: 2,"
	LABELS "bench")

# M9: a scene that needs a capability the backend lacks (the joint scenes on Null, which never gets joints) is refused before
# the backend is initialised: exit 2, status / category unsupported, exit.json only (the runner reports the cell as unsupported).
pt_add_bench_exit_test(ExitSceneNeedsJoints 2 "does not support scenario M6a.Pendulum: it needs PT_CAP_JOINT_HINGE" unsupported
	ARGS -Backend=Null -Scenario=M6a.Pendulum ${_pt_ctest_run_args} EXIT_REGEX ".category.: .unsupported.")
pt_add_bench_exit_test(ExitSceneNeedsBallJoints 2 "it needs PT_CAP_JOINT_BALL" unsupported
	ARGS -Backend=NullDll -Scenario=M6a.Pendulum -Param:Joint=Ball ${_pt_ctest_run_args})
# M9: a behaviour run that records states also writes scene_desc.json (the scene as marshalled: bodies, shapes, joints) and every
# run with a preflight writes preflight_diff.json (resolved vs effective); the manifest names the forced overrides it applied.
pt_add_script_test(PhysicsTest.Bench.BehaviorOutputs SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=0 "-DPT_EXPECT_REGEX=preflight diff .resolved vs effective"
		"-DPT_ARGS=-Backend=Null|-Scenario=M7.UEDefaults|-Frames=10|-RunMode=Behavior|-StatesEvery=1|${_pt_ctest_run_args}|-Out=${_pt_bench_dir}/BehaviorOutputs"
		"-DPT_EXPECT_FILE=${_pt_bench_dir}/BehaviorOutputs/scene_desc.json" "-DPT_EXPECT_FILE_REGEX=.schema.: .physicstest.scene_desc/1.*.name.: .Cube100.*.mass.: 177[.]8"
		"-DPT_EXPECT_FILE2=${_pt_bench_dir}/BehaviorOutputs/preflight_diff.json" "-DPT_EXPECT_FILE2_REGEX=.schema.: .physicstest.preflight_diff/1.*.key.: .world.gravity."
		"-DPT_EXPECT_FILE3=${_pt_bench_dir}/BehaviorOutputs/manifest.json" "-DPT_EXPECT_FILE3_REGEX=.forced_overrides.: [{].*.applied.: true.*.required_capabilities.: [[][]]"
	LABELS "bench")

# G9 / G10a / G0 refusals (03 M7: "G9 불일치가 하나라도 있으면 preflight가 exit 2로 거부한다 (주입 테스트)", "계측 tier 0인 bench
# 모드 run에 listener나 드로어가 붙어 있으면 preflight가 exit 2로 거부한다 (주입 테스트)"). -InjectPreflightFault corrupts the value a
# gate reads; -InjectSceneInstrumentFlags=0x8 gives the backend PT_INSTR_CONTACTS behind the run's back, so Jolt / PhysX really
# install their contact listener in a run that declares tier 0.
pt_add_bench_exit_test(PreflightG9Injected 2 "preflight G9 .*: Fail" preflight_failed
	ARGS -Backend=${_pt_bench_option_backend} -Scenario=M3.BoxPile -Frames=2 -InjectPreflightFault=g9:world.gravity
	MANIFEST_REGEX ".status.: .preflight_failed.*.id.: .G9.,[^}]*.result.: .Fail.")
pt_add_bench_exit_test(PreflightG10aListenerInjected 2 "a listener is installed in a tier-0 bench run" preflight_failed
	ARGS -Backend=Null -Scenario=M3.BoxPile -Frames=2 -InjectPreflightFault=listener
	MANIFEST_REGEX ".id.: .G10a.,[^}]*.result.: .Fail.")
pt_add_bench_exit_test(PreflightG10aDrawerInjected 2 "a debug drawer is attached in a tier-0 bench run" preflight_failed
	ARGS -Backend=Null -Scenario=M3.BoxPile -Frames=2 -InjectPreflightFault=drawer
	MANIFEST_REGEX ".id.: .G10a.,[^}]*.result.: .Fail.")
pt_add_bench_exit_test(PreflightG0Injected 2 "preflight G0 .*: Fail" preflight_failed
	ARGS -Backend=Null -Scenario=S00.FreeFall -Frames=2 -InjectPreflightFault=g0
	MANIFEST_REGEX ".id.: .G0.,[^}]*.result.: .Fail.")
if(PT_WITH_JOLT)
	pt_add_bench_exit_test(PreflightG10aRealListener 2 "a listener is installed in a tier-0 bench run" preflight_failed
		ARGS -Backend=Jolt -Scenario=M3.BoxPile -Frames=2 -InjectSceneInstrumentFlags=0x8
		MANIFEST_REGEX ".id.: .G10a.,[^}]*.result.: .Fail.")
endif()
if(PT_WITH_BULLET)
	pt_add_bench_exit_test(PreflightG10aBulletDrawerInjected 2 "a debug drawer is attached in a tier-0 bench run: bullet.debug_drawer" preflight_failed
		ARGS -Backend=Bullet -Scenario=M3.BoxPile -Frames=2 -InjectPreflightFault=drawer
		MANIFEST_REGEX ".id.: .G10a.,[^}]*.result.: .Fail.")
endif()

# M7 UE_RUNTIME (02 12.2 step 13; 03 M7: "UE_RUNTIME 플래그를 켠 Null DLL 시험 빌드로 TerminateProcess 경로를 확인한다"): with
# PT_NULL_DLL_UE_RUNTIME=1 the Null DLL declares PT_MODULE_UE_RUNTIME; the run writes its outputs and exit.json (terminate_process
# true), then TerminateProcess ends the process, so the DLL never reaches DLL_PROCESS_DETACH (PT_NULL_DLL_REPORT_DETACH=1 prints a
# line there). The control run without the flag ends normally and prints it.
pt_add_bench_exit_test(UeRuntimeTerminate 0 "TerminateProcess[(]GetCurrentProcess[(][)], 0[)]" ok
	ARGS -Backend=NullDll -Scenario=S00.FreeFall -Frames=5 ${_pt_ctest_run_args}
	MANIFEST_REGEX ".status.: .ok."
	FORBID_REGEX "DLL_PROCESS_DETACH"
	ENVIRONMENT PT_NULL_DLL_UE_RUNTIME=1 PT_NULL_DLL_REPORT_DETACH=1)
# M7 fix round 0: a Fatal with the UE_RUNTIME module loaded writes exit.json (3, terminate_process true) and ends with
# TerminateProcess too - no DLL_PROCESS_DETACH after a Fatal either.
pt_add_bench_exit_test(UeRuntimeFatal 3 "TerminateProcess[(]GetCurrentProcess[(][)], 3[)]: a Fatal with a PT_MODULE_UE_RUNTIME module loaded" fatal
	ARGS -Backend=NullDll -Scenario=S00.FreeFall -Frames=5 -InjectFatal ${_pt_ctest_run_args}
	EXIT_REGEX ".terminate_process.: true"
	FORBID_REGEX "DLL_PROCESS_DETACH"
	ENVIRONMENT PT_NULL_DLL_UE_RUNTIME=1 PT_NULL_DLL_REPORT_DETACH=1)
pt_add_bench_exit_test(UeRuntimeControl 0 "DLL_PROCESS_DETACH at process exit" ok
	ARGS -Backend=NullDll -Scenario=S00.FreeFall -Frames=5 ${_pt_ctest_run_args}
	FORBID_REGEX "TerminateProcess[(]GetCurrentProcess"
	ENVIRONMENT PT_NULL_DLL_REPORT_DETACH=1)
pt_add_script_test(PhysicsTest.Bench.UeRuntimeDescribe SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=0 "-DPT_EXPECT_REGEX=TerminateProcess[(]GetCurrentProcess[(][)], 0[)]"
		"-DPT_ARGS=-Describe|-Backend=NullDll" "-DPT_FORBID_REGEX=DLL_PROCESS_DETACH"
	LABELS "bench")
set_tests_properties(PhysicsTest.Bench.UeRuntimeDescribe PROPERTIES ENVIRONMENT "PT_NULL_DLL_UE_RUNTIME=1;PT_NULL_DLL_REPORT_DETACH=1")

# M7 multi-process gates on Null, the Null DLL and every SDK backend / variant (short runs; the 600-frame evidence is manual).
pt_add_script_test(PhysicsTest.Bench.G8Det1 SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunGateMatrix.cmake"
	ARGS "-DPT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_OUT_DIR=${_pt_bench_dir}/G8" -DPT_GATE=G8 "-DPT_BACKEND=${_pt_bench_backends}" -DPT_FRAMES=60
	LABELS "bench")
pt_add_script_test(PhysicsTest.Bench.G11ContactCapture SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunGateMatrix.cmake"
	ARGS "-DPT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_OUT_DIR=${_pt_bench_dir}/G11" -DPT_GATE=G11 "-DPT_BACKEND=${_pt_bench_backends}" -DPT_FRAMES=60
	LABELS "bench")
# M7 fix round 0: the negative controls - -InjectGateFault=4 perturbs one child after frame 4 (G8: the last run, G11: contacts
# on), so both comparators must report the difference and exit 2.
pt_add_script_test(PhysicsTest.Bench.G8NegativeControl SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=2 "-DPT_EXPECT_REGEX=G8 FAIL h_native differs"
		"-DPT_ARGS=-Gate=G8|-Runs=2|-Backend=Null|-Scenario=M3.BoxPile|-Frames=10|-InjectGateFault=4|${_pt_ctest_run_args}|-Out=${_pt_bench_dir}/G8NegativeControl"
		"-DPT_EXPECT_FILE=${_pt_bench_dir}/G8NegativeControl/gate_g8.json" "-DPT_EXPECT_FILE_REGEX=.result.: .fail.,.*h_state differs.*.injected_gate_fault_frame.: 4"
		"-DPT_EXPECT_FILE2=${_pt_bench_dir}/G8NegativeControl/exit.json" "-DPT_EXPECT_FILE2_REGEX=.exit_code.: 2,.*.status.: .gate_failed."
	LABELS "bench")
# M9 (03 M9 G11 addition, -G11Knob=variants): the variant gate on one body (M1.SingleBoxDrop b, which tips over on the ground):
# every frame's hashes of Bullet.StaticAabbOff equal Bullet.Default's. M9 prep finding F-1 (docs/Progress.md M9): on
# M3.BoxPile 1k the two diverge from frame 51 (UEParity) / 54 (Normalized) - with DBVT_BP_PREVENTFALSEUPDATE 0 a forced AABB
# update moves the static ground's proxy from the fixed to the dynamic tree every step (btDbvtBroadphase.cpp:319-329 [V]),
# which changes the pair order of a pile. M9 verdict (Bullet implementer, integrator): the criterion holds on one-body, joint
# and tower scenes in both profiles and on the pile with bEnableEnhancedDeterminism under Normalized (Bullet then sorts pairs
# and island manifolds by body id, btOverlappingPairCache.cpp:374, btSimulationIslandManager.cpp:376-378 [V]); the second run
# pins the pile past the default profile's divergence frame. The default-profile pile difference is tagged algorithmic.
if(PT_WITH_BULLET)
	pt_add_script_test(PhysicsTest.Bench.G11BulletStaticAabbOff SCRIPT "${_pt_exit_script}"
		ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=0 "-DPT_EXPECT_REGEX=G11 Bullet: Pass"
			"-DPT_ARGS=-Gate=G11|-G11Knob=variants:Default,StaticAabbOff|-Backend=Bullet|-Profile=UEParity|-Scenario=M1.SingleBoxDrop|-Param:Case=b|${_pt_ctest_run_args}|-Out=${_pt_bench_dir}/G11BulletStaticAabbOff"
			"-DPT_EXPECT_FILE=${_pt_bench_dir}/G11BulletStaticAabbOff/gate_g11.json" "-DPT_EXPECT_FILE_REGEX=.result.: .pass.,.*variant_Default.*variant_StaticAabbOff"
		LABELS "bench")
	pt_add_script_test(PhysicsTest.Bench.G11BulletStaticAabbOffPile SCRIPT "${_pt_exit_script}"
		ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=0 "-DPT_EXPECT_REGEX=G11 Bullet: Pass"
			"-DPT_ARGS=-Gate=G11|-G11Knob=variants:Default,StaticAabbOff|-Backend=Bullet|-Profile=Normalized|-Scenario=M3.BoxPile|-Frames=120|-ini:Engine:[/Script/Engine.PhysicsSettings]:bEnableEnhancedDeterminism=True|${_pt_ctest_run_args}|-Out=${_pt_bench_dir}/G11BulletStaticAabbOffPile"
			"-DPT_EXPECT_FILE=${_pt_bench_dir}/G11BulletStaticAabbOffPile/gate_g11.json" "-DPT_EXPECT_FILE_REGEX=.result.: .pass.,.*variant_Default.*variant_StaticAabbOff"
		LABELS "bench")
	# 03 M9: the Bullet viz drawer attached (viz child: -RunMode=Viz -Instrument=native_debug_draw -CaptureEvery=1, so
	# debugDrawWorld runs every frame) and not attached end on equal hashes, every frame (UEParity, sleep on: M7.UEDefaults puts
	# its six bodies to sleep by frame 98 under the UE thresholds).
	pt_add_script_test(PhysicsTest.Bench.G11BulletVizDrawer SCRIPT "${_pt_exit_script}"
		ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=0 "-DPT_EXPECT_REGEX=G11 Bullet: Pass"
			"-DPT_ARGS=-Gate=G11|-G11Knob=viz|-Backend=Bullet|-Profile=UEParity|-Scenario=M7.UEDefaults|-Frames=150|${_pt_ctest_run_args}|-Out=${_pt_bench_dir}/G11BulletVizDrawer"
			"-DPT_EXPECT_FILE=${_pt_bench_dir}/G11BulletVizDrawer/gate_g11.json" "-DPT_EXPECT_FILE_REGEX=.result.: .pass.,.*viz on: 150 captured frames, [1-9][0-9]* native draw commands"
		LABELS "bench")
	# M9 (03 M9 "the bench exe verifies the native draw without a viewer, recording the command stream only"): native_draw.csv.
	pt_add_bench_exit_test(NativeDrawBullet 0 "status ok" ok
		ARGS -Backend=Bullet -Scenario=M1.SingleBoxDrop -Param:Case=a -Frames=30 -RunMode=Viz -Instrument=native_debug_draw ${_pt_ctest_run_args}
		MANIFEST_REGEX ".native_draw.: [{][^}]*.frames.: 30,[^}]*.commands.: [1-9][0-9]*,[^}]*.truncated_frames.: 0,")
endif()
if(PT_WITH_PHYSX)
	pt_add_bench_exit_test(NativeDrawPhysX 0 "status ok" ok
		ARGS -Backend=PhysX -Scenario=M1.SingleBoxDrop -Param:Case=a -Frames=30 -RunMode=Viz -Instrument=native_debug_draw ${_pt_ctest_run_args}
		MANIFEST_REGEX ".native_draw.: [{][^}]*.frames.: 30,[^}]*.commands.: [1-9][0-9]*,[^}]*.truncated_frames.: 0,")
	pt_add_script_test(PhysicsTest.Bench.G11PhysXViz SCRIPT "${_pt_exit_script}"
		ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=0 "-DPT_EXPECT_REGEX=G11 PhysX: Pass"
			"-DPT_ARGS=-Gate=G11|-G11Knob=viz|-Backend=PhysX|-Profile=UEParity|-Scenario=M6b.Chain|-Frames=60|${_pt_ctest_run_args}|-Out=${_pt_bench_dir}/G11PhysXViz"
			"-DPT_EXPECT_FILE=${_pt_bench_dir}/G11PhysXViz/gate_g11.json" "-DPT_EXPECT_FILE_REGEX=.result.: .pass.,.*viz on: 60 captured frames, [1-9][0-9]* native draw commands"
		LABELS "bench")
endif()
# M9 fix round 0: the viz knob on a backend that draws nothing (Null: no PT_CAP_NATIVE_DEBUG_DRAW) must fail - the gate asserts
# that the viz child drew in every frame instead of comparing two identical bench runs.
pt_add_script_test(PhysicsTest.Bench.G11VizNoDrawing SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=2 "-DPT_EXPECT_REGEX=G11 FAIL viz knob without a drawing: the on child recorded 0 native draw commands"
		"-DPT_ARGS=-Gate=G11|-G11Knob=viz|-Backend=Null|-Scenario=M1.SingleBoxDrop|-Param:Case=a|-Frames=10|${_pt_ctest_run_args}|-Out=${_pt_bench_dir}/G11VizNoDrawing"
		"-DPT_EXPECT_FILE=${_pt_bench_dir}/G11VizNoDrawing/gate_g11.json" "-DPT_EXPECT_FILE_REGEX=.result.: .fail."
	LABELS "bench")
pt_add_script_test(PhysicsTest.Bench.G11NegativeControl SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=2 "-DPT_EXPECT_REGEX=G11 FAIL hashes.csv differs at data row 6: off '5,"
		"-DPT_ARGS=-Gate=G11|-Backend=Null|-Scenario=M3.BoxPile|-Frames=10|-InjectGateFault=4|${_pt_ctest_run_args}|-Out=${_pt_bench_dir}/G11NegativeControl"
		"-DPT_EXPECT_FILE=${_pt_bench_dir}/G11NegativeControl/gate_g11.json" "-DPT_EXPECT_FILE_REGEX=.result.: .fail.,.*.injected_gate_fault_frame.: 4"
	LABELS "bench")
# M7: the preflight passes on every registered scene x backend / variant x profile (S08 / Normalized refused by design). M9: the
# new variants Jolt.SharpBoxes and Bullet.StaticAabbOff join the sweep; every SDK backend runs the joint scenes, only Null's
# "unsupported" refusals of them are tolerated and listed (RunPreflightSweep.cmake).
set(_pt_sweep_backends "${_pt_bench_backends}")
if(PT_WITH_JOLT)
	string(APPEND _pt_sweep_backends ",Jolt/SharpBoxes")
endif()
if(PT_WITH_BULLET)
	string(APPEND _pt_sweep_backends ",Bullet/StaticAabbOff")
endif()
pt_add_script_test(PhysicsTest.Bench.PreflightSweep SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunPreflightSweep.cmake"
	ARGS "-DPT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_OUT_DIR=${_pt_bench_dir}/PreflightSweep" "-DPT_BACKEND=${_pt_sweep_backends}"
	LABELS "bench")
# M7 thread mapping (02 9.6): Jolt and PhysX T = 1..6 and Bullet T = 1 (T = 2 refused); the masks and engine workers recorded.
set(_pt_thread_engines "")
if(PT_WITH_JOLT)
	list(APPEND _pt_thread_engines Jolt)
endif()
if(PT_WITH_PHYSX)
	list(APPEND _pt_thread_engines PhysX)
endif()
string(REPLACE ";" "," _pt_thread_engines "${_pt_thread_engines}")
if(_pt_thread_engines)
	pt_add_script_test(PhysicsTest.Bench.ThreadMatrix SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunThreadMatrix.cmake"
		ARGS "-DPT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_OUT_DIR=${_pt_bench_dir}/ThreadMatrix" "-DPT_ENGINES=${_pt_thread_engines}"
			-DPT_BULLET=${PT_WITH_BULLET} -DPT_FRAMES=10
		LABELS "bench")
	# Up to six pinned P-cores at HIGH priority: alone, so that it neither starves nor is starved by the other CTest processes.
	set_tests_properties(PhysicsTest.Bench.ThreadMatrix PROPERTIES RUN_SERIAL ON)
endif()

# M8 (M7 review): the positive control of the allocation hook - one operator new inside frame 3's Tick (the scene's PreTick
# delegate, test only) must be counted: tick_new_calls 1, the first allocating frame 3, and the run is not headline-valid.
pt_add_bench_exit_test(AllocHookPositiveControl 0 "status ok" ok
	ARGS -Backend=Null -Scenario=S00.FreeFall -Frames=6 -InjectTickAllocation=3 ${_pt_ctest_run_args}
	MANIFEST_REGEX ".timing_valid_for_headline.: false,.*.timing_invalid_reasons.: [[][^]]*1 allocation[(]s[)] inside the timed buckets [(]first at frame 3[)].*.test_injections.: [{][^}]*.tick_allocation_frame.: 3.*.tick_new_calls.: 1,.*.tick_first_allocating_frame.: 3,")
# M8 (M7 review): -InjectPreflightFault=g9:<key> on a key G9 does not compare (not READBACK, or no such key) is a usage error
# (exit 2, status usage_error), never a silent pass or an ordinary G9 refusal.
pt_add_bench_exit_test(PreflightG9InjectedNotCompared 2 "G9 does not compare 'solver.iterations'" usage_error
	ARGS -Backend=Null -Scenario=M3.BoxPile -Frames=2 -InjectPreflightFault=g9:solver.iterations
	MANIFEST_REGEX ".status.: .usage_error.*.preflight_error.: .*G9 does not compare 'solver.iterations'")
pt_add_bench_exit_test(PreflightG9InjectedUnknownKey 2 "G9 does not compare 'no.such.key' on M3.BoxPile [(]no such effective key[)]" usage_error
	ARGS -Backend=Null -Scenario=M3.BoxPile -Frames=2 -InjectPreflightFault=g9:no.such.key)
# M8 (M7 review): a Fatal in -Conformance with a UE_RUNTIME module loaded writes exit.json (3, fatal) and ends with
# TerminateProcess - no DLL_PROCESS_DETACH (M13 runs every Chaos gate through -Conformance).
pt_add_bench_exit_test(UeRuntimeConformanceFatal 3 "TerminateProcess[(]GetCurrentProcess[(][)], 3[)]: a Fatal with a PT_MODULE_UE_RUNTIME module loaded" fatal
	ARGS -Conformance -Backend=NullDll -InjectFatal
	EXIT_REGEX ".terminate_process.: true"
	FORBID_REGEX "DLL_PROCESS_DETACH"
	ENVIRONMENT PT_NULL_DLL_UE_RUNTIME=1 PT_NULL_DLL_REPORT_DETACH=1)
# M8: the runner's "died half-way" case - the process ends with a raw exit code 0 after frame 2 and leaves no exit.json
# (CheckFatalExit: PT_EXPECT_NO_FILE). Tools/ptbench must report such a run as failed (PhysicsTest.Py.Integration).
pt_add_script_test(PhysicsTest.Bench.InjectSilentExit SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=0 "-DPT_EXPECT_REGEX=test-only -InjectSilentExit"
		"-DPT_ARGS=-Backend=Null|-Scenario=S00.FreeFall|-Frames=6|-InjectSilentExit=2|${_pt_ctest_run_args}|-Out=${_pt_bench_dir}/InjectSilentExit"
		"-DPT_EXPECT_NO_FILE=${_pt_bench_dir}/InjectSilentExit/exit.json"
	LABELS "bench")
# M8: -AnalyticReferences prints the C++ references as JSON (Tools/ptbench/analytic.py compares them: PhysicsTest.Py.AnalyticMatchesCpp).
add_test(NAME PhysicsTest.Bench.AnalyticReferences COMMAND PhysicsTestBench -AnalyticReferences)
set_tests_properties(PhysicsTest.Bench.AnalyticReferences PROPERTIES LABELS "bench"
	PASS_REGULAR_EXPRESSION ".id.: .S00[.]free_fall_dt_engine.*.z.: 501[.]83328137")
# M10 (03 M10 criterion 1): the ABI call overhead on the TSC - Null, 0 bodies, 4 calls per frame, the median over batches below
# 100 ns per frame (exit 0; a Debug build records it, "gate": "recorded").
pt_add_script_test(PhysicsTest.Bench.AbiOverhead SCRIPT "${_pt_exit_script}"
	ARGS "-DPT_EXE=$<TARGET_FILE:PhysicsTestBench>" -DPT_EXPECT_RC=0 "-DPT_EXPECT_REGEX=.median_ns_per_frame.: [0-9.]+,.*.gate.: .(pass|recorded)"
		"-DPT_ARGS=-AbiOverhead|-Backend=Null"
	LABELS "bench")
# M10 (03 M10 criterion 2, 02 14.3 G10b): a protocol run (T = 1, pre-roll 0.5 s, M3.BoxPile 1k, 300 frames) of every SDK backend
# proves its single thread: the census window's cpu_eff in [0.98, 1.02] and the other threads of the process below 0.5 % of its
# cycles. Measured on this process's own threads (TimingContract 7.1). M10 fix round 1: these tests need a verdict, and a
# window that parallel CTest benches on CPU 0 starve may have none (G10b Skip "contended" - in the prof build the Tracy
# client's thread runs in proportion to wall time); RUN_SERIAL gives them the core (a few seconds each, ~20 s in Debug).
foreach(_pt_g10b_backend IN ITEMS Jolt Bullet PhysX)
	string(TOUPPER "${_pt_g10b_backend}" _pt_g10b_upper)
	if(PT_WITH_${_pt_g10b_upper})
		pt_add_bench_exit_test(G10b${_pt_g10b_backend} 0 "G10b [^:]*: Pass" ok
			ARGS -Backend=${_pt_g10b_backend} -Scenario=M3.BoxPile -Frames=300 -PreRoll=0.5 -HostCheck=record
			MANIFEST_REGEX ".census_window.: [{].*.foreign_cycles_fraction.: [0-9].*.id.: .G10b.,[^}]*.result.: .Pass.")
		set_tests_properties(PhysicsTest.Bench.G10b${_pt_g10b_backend} PROPERTIES RUN_SERIAL ON)
	endif()
endforeach()
# M10 fix round 0: the G10b negative control (like G9's -InjectPreflightFault and G12's -InjectEngineErrorBits). A helper thread
# spins 20 ms of its own CPU time inside the census window of the same protocol run (-InjectForeignWork=20, test only) and ends
# before census #2, so it is in neither census: G10b must still fail (the process total shows its cycles as unattributed), and
# the run loses headline validity with the G10b reason and stays unofficial. A Debug build steps far slower (dbg: a 20 s window
# for 300 frames, where 20 ms is 0.1 %): 30 frames and 200 ms there - the helper then usually still spins at census #2 and is
# judged as a new thread (foreign), the other path of the same proof.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(_pt_g10b_neg_frames 30)
	set(_pt_g10b_neg_ms 200)
else()
	set(_pt_g10b_neg_frames 300)
	set(_pt_g10b_neg_ms 20)
endif()
pt_add_bench_exit_test(G10bNegativeControl 0 "G10b [^:]*: Fail" ok
	ARGS -Backend=${_pt_bench_option_backend} -Scenario=M3.BoxPile -Frames=${_pt_g10b_neg_frames} -PreRoll=0.5 -HostCheck=record
		-InjectForeignWork=${_pt_g10b_neg_ms}
	MANIFEST_REGEX ".timing_valid_for_headline.: false,.*G10b failed [(]T = 1 single-thread proof[)]: foreign thread cycles [0-9.]+ % [+] unattributed [0-9.]+ %.*.unofficial.: true,.*.test_injections.: [{][^}]*.foreign_work_ms.: ${_pt_g10b_neg_ms}.*.id.: .G10b.,[^}]*.result.: .Fail.")
set_tests_properties(PhysicsTest.Bench.G10bNegativeControl PROPERTIES RUN_SERIAL ON)   # a verdict, like the G10b tests above
# M10 (03 M10 task 4, 02 12.4): -Conformance writes a valid stamp for the whole suite (G10b included) into -StampDir, a run of the
# same configuration finds it (manifest conformance_stamp valid), and a run with an engine-key override does not (the key holds
# the -ini: / -ExecCmds= switches). The runner's refusals (variant ini, DLL, tolerances.json) are PhysicsTest.Py.Integration.
pt_add_script_test(PhysicsTest.Bench.ConformanceStamp SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunStampCheck.cmake"
	ARGS "-DPT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_OUT_DIR=${_pt_bench_dir}/ConformanceStamp" "-DPT_BACKEND=${_pt_bench_option_backend}"
	LABELS "bench")

# M8 (M7 review): the headline positive control - a protocol-conformant run (tier 0, pre-roll 0.5 s, no injection) is
# headline-valid except for what this machine and preset cannot give: in test / test-avx2 only a host contamination recorded
# under -HostCheck=record may remain, in dev / dbg also the PT_DO_CHECK reason. RunHeadlineControl.cmake reads the manifest.
pt_add_script_test(PhysicsTest.Bench.HeadlinePositiveControl SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunHeadlineControl.cmake"
	ARGS "-DPT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_OUT_DIR=${_pt_bench_dir}/HeadlinePositiveControl"
	LABELS "bench")

# 03 M7: the manifest of an M3.BoxPile 5k run passes Config/Schemas/manifest.schema.json (jsonschema in the repository .venv,
# python -I) with the criterion's fields filled; a broken copy must fail (the schema is not vacuous). PT_PYTHON and the
# jsonschema probe come from cmake/PTPython.cmake: without jsonschema the test is reported as Skipped with an install hint
# (M8, M7 review), never run against an interpreter that cannot import it.
pt_python_has(jsonschema _pt_have_jsonschema _pt_jsonschema_hint)
set(_pt_schema_backend ${_pt_bench_option_backend})
if(_pt_have_jsonschema)
	pt_add_script_test(PhysicsTest.Bench.ManifestSchema SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunManifestSchema.cmake"
		ARGS "-DPT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "-DPT_OUT_DIR=${_pt_bench_dir}/ManifestSchema" "-DPT_PYTHON=${PT_PYTHON}"
			"-DPT_VALIDATOR=${CMAKE_SOURCE_DIR}/Tools/validate_manifest.py" -DPT_BACKEND=${_pt_schema_backend}
		LABELS "bench")
else()
	pt_add_skipped_python_test(PhysicsTest.Bench.ManifestSchema "${_pt_jsonschema_hint}")
endif()

# M7 fix round 0 (docs/ResultsSchema.md 1): every manifest.json and exit.json the bench tests above wrote validates against
# Config/Schemas - refused, failed, contaminated and instrumented runs included (the G8 / G11 children too). It runs after them
# (DEPENDS on every test of this directory); a directory a run of this build did not write keeps its old content or is absent
# (skipped), so run the whole label, not this test alone.
if(NOT _pt_have_jsonschema)
	pt_add_skipped_python_test(PhysicsTest.Bench.ManifestsValidate "${_pt_jsonschema_hint}")
else()
	get_property(_pt_bench_tests DIRECTORY PROPERTY TESTS)
	set(_pt_manifest_dirs "${_pt_bench_dir}/G8" "${_pt_bench_dir}/G11")
	foreach(_test IN LISTS _pt_bench_tests)
		string(REGEX REPLACE "^PhysicsTest[.]Bench[.]" "" _name "${_test}")
		list(APPEND _pt_manifest_dirs "${_pt_bench_dir}/${_name}")
	endforeach()
	string(REPLACE ";" "|" _pt_manifest_dirs_arg "${_pt_manifest_dirs}")
	pt_add_script_test(PhysicsTest.Bench.ManifestsValidate SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunManifestSchema.cmake"
		ARGS "-DPT_PYTHON=${PT_PYTHON}" "-DPT_VALIDATOR=${CMAKE_SOURCE_DIR}/Tools/validate_manifest.py" "-DPT_VALIDATE_DIRS=${_pt_manifest_dirs_arg}"
		LABELS "bench")
	set_tests_properties(PhysicsTest.Bench.ManifestsValidate PROPERTIES DEPENDS "${_pt_bench_tests}")
endif()
