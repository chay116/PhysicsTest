# Source/Tests/PtBench - CTest entries of Tools/ptbench (03_WorkPlan M8: "테스트로 확인한다"). They run the repository .venv's
# interpreter with -I against the editable install (pip install --no-build-isolation --no-deps -e Tools/ptbench); without it
# they are reported as Skipped with the install hint (cmake/PTPython.cmake, M7 review: never a silent fallback).
#   PhysicsTest.Py.Unit                 the pure-Python tests (shuffle, matrices, repeated / runner bench switches refused,
#                                       analytic (and its one-line exit 2 for unreadable C++ references), statistics,
#                                       validate rules, a refused table without statistics, the report head from every
#                                       input, the condition under every table and above every figure, a window without
#                                       values, parquet column types, determinism with a majority reference and the
#                                       incomplete verdict, the environment gate with an injected power plan, the idle
#                                       wait, the runner's teardown kill / timeout / kill on interruption / job object,
#                                       executables by full path, UTF-8 console output, schemas; M10: the claim rule's three
#                                       synthetic cases, bootstrap CIs, the CV gate's extra blocks / noisy and the sentinel
#                                       drift flag with synthetic results (runner end to end with a mocked bench), stamp
#                                       verification (missing / stale / invalid), comparability keys, viewer manifests,
#                                       S(T) / E(T), missing CLI paths, the composition control table, target selection)
#   PhysicsTest.Py.AnalyticMatchesCpp   analytic.py against PhysicsTestBench -AnalyticReferences, |difference| <= 1e-9
#   PhysicsTest.Py.Integration          the runner and the analysis with this build's bench exe: a refused environment
#                                       gate starts no run, a run that died half-way
#                                       (raw exit code 0, no exit.json) is a failure, a timeout too, exit 4 is retried (never
#                                       skipped), the matrix / target / --bench-arg switches read back from the manifests, a viz
#                                       manifest is rejected, Shipped + MatchedAVX2 or two desc_sha256 in one table are
#                                       errors, the DET1 matrix and the first divergent frame of a tampered copy, a session end
#                                       to end with its report (also from its run directories alone) and `ptbench perf` with
#                                       a captured stdout; M10: conformance stamps refuse a stale cell before any process
#                                       (a PhysX.UE4Compat.ini key, the Null DLL, tolerances.json), a measurement-protocol run
#                                       proves G10b and calibrates the TSC; single-SDK builds (test-<engine>) run the engines
#                                       they have
set(_pt_py_out "${CMAKE_BINARY_DIR}/_py")
pt_add_python_test(PhysicsTest.Py.Unit REQUIRES ptbench
	COMMAND -m unittest -v ptbench.tests.test_unit
	LABELS "py")
pt_add_python_test(PhysicsTest.Py.AnalyticMatchesCpp REQUIRES ptbench
	COMMAND -m ptbench analytic "--check-cpp=$<TARGET_FILE:PhysicsTestBench>"
	LABELS "py")
pt_add_python_test(PhysicsTest.Py.Integration REQUIRES ptbench
	COMMAND -m unittest -v ptbench.tests.test_integration
	LABELS "py"
	ENVIRONMENT "PT_BENCH_EXE=$<TARGET_FILE:PhysicsTestBench>" "PT_TEST_OUT=${_pt_py_out}/Integration")
if(TEST PhysicsTest.Py.Integration)
	# Its runs use CPU 0 at HIGH priority like the other bench tests; -HostCheck=record keeps them from failing each other.
	# The exit-4 retry case forces contamination with -ContaminationPct=0 and does not depend on the neighbours.
	set_tests_properties(PhysicsTest.Py.Integration PROPERTIES TIMEOUT 900)
endif()
