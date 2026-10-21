# RunContaminated.cmake - 03_WorkPlan M7: exit code 4 "오염된 호스트" reproduced by another process's load ("4는 다른 프로세스
# 부하를 주입해 오염된 호스트 판정을 재현한다"). Two processes run concurrently (one execute_process with two COMMANDs): a busy
# helper (this exe with -BusyHelper=<s>) spinning a CPU at HIGH_PRIORITY_CLASS, and a bench run pinned to CPU 0 (-Affinity=0x1,
# also HIGH). The bench measures other processes on its cores - CPU 0 and its SMT sibling (docs/TimingContract.md 5.1) - so
# it must exit 4 with exit.json saying contaminated_host. The helper ends by itself after <s> seconds and execute_process
# waits for both: nothing is left running.
#   PT_TARGET=cpu0     (default) the helper spins CPU 0 itself: the two processes share the logical CPU
#   PT_TARGET=sibling  (M7 fix round 0) the helper spins CPU 0's SMT sibling (read from a probe run's threads.smt_sibling_mask):
#                      the bench's own CPU is free, but the core it runs on is shared. Skipped (PASS with a note) on a CPU
#                      without SMT
#   cmake -DPT_BENCH_EXE=<exe> -DPT_OUT_DIR=<dir> [-DPT_HELPER_SECONDS=4] [-DPT_TARGET=cpu0|sibling] -P RunContaminated.cmake
cmake_minimum_required(VERSION 3.25)
if(NOT PT_HELPER_SECONDS)
	set(PT_HELPER_SECONDS 4)
endif()
if(NOT PT_TARGET)
	set(PT_TARGET cpu0)
endif()
file(REMOVE_RECURSE "${PT_OUT_DIR}")
file(MAKE_DIRECTORY "${PT_OUT_DIR}")
set(_helper_mask 0x1)
if(PT_TARGET STREQUAL "sibling")
	# A short probe run (record only) reports the SMT sibling of CPU 0 from the processor topology.
	execute_process(COMMAND "${PT_BENCH_EXE}" -Backend=Null -Scenario=S00.FreeFall -Frames=1 -PreRoll=0 -HostCheck=record -Affinity=0x1 "-Out=${PT_OUT_DIR}/probe"
		RESULT_VARIABLE _rc OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 60)
	if(NOT _rc EQUAL 0)
		message(FATAL_ERROR "the probe run exited ${_rc}\n${_stdout}${_stderr}")
	endif()
	file(READ "${PT_OUT_DIR}/probe/manifest.json" _probe)
	string(JSON _helper_mask GET "${_probe}" threads smt_sibling_mask)
	if(_helper_mask STREQUAL "0x0")
		message(STATUS "PASS (skipped): CPU 0 has no SMT sibling on this machine")
		return()
	endif()
elseif(NOT PT_TARGET STREQUAL "cpu0")
	message(FATAL_ERROR "PT_TARGET=${PT_TARGET}: cpu0 or sibling")
endif()
# The helper writes nothing to stdout; its stdout is the bench's stdin, which the bench never reads.
execute_process(
	COMMAND "${PT_BENCH_EXE}" "-BusyHelper=${PT_HELPER_SECONDS}" "-Affinity=${_helper_mask}"
	COMMAND "${PT_BENCH_EXE}" -Backend=Null -Scenario=S00.FreeFall -Frames=60 -PreRoll=1.5 -Affinity=0x1 "-Out=${PT_OUT_DIR}/run"
	RESULTS_VARIABLE _rcs OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 120)
message(STATUS "exit codes (helper on ${_helper_mask}, bench on 0x1): ${_rcs}\n${_stdout}${_stderr}")
list(GET _rcs 0 _helper_rc)
list(GET _rcs 1 _bench_rc)
if(NOT _helper_rc EQUAL 0)
	message(FATAL_ERROR "the busy helper exited ${_helper_rc}")
endif()
if(NOT _bench_rc EQUAL 4)
	message(FATAL_ERROR "the bench exited ${_bench_rc}, expected 4 (contaminated host)")
endif()
if(NOT EXISTS "${PT_OUT_DIR}/run/exit.json" OR NOT EXISTS "${PT_OUT_DIR}/run/manifest.json")
	message(FATAL_ERROR "exit.json / manifest.json missing")
endif()
file(READ "${PT_OUT_DIR}/run/exit.json" _exit)
string(JSON _code GET "${_exit}" exit_code)
string(JSON _status GET "${_exit}" status)
file(READ "${PT_OUT_DIR}/run/manifest.json" _manifest)
string(JSON _core GET "${_manifest}" platform host_load core_busy_other_pct)
string(JSON _mask_other GET "${_manifest}" platform host_load mask_busy_other_pct)
string(JSON _sibling GET "${_manifest}" platform host_load smt_sibling_busy_pct)
string(JSON _limit GET "${_manifest}" platform host_load threshold_total_pct)
string(JSON _valid GET "${_manifest}" timing_valid_for_headline)
if(NOT _code EQUAL 4 OR NOT _status STREQUAL "contaminated_host" OR NOT _valid STREQUAL "OFF")
	message(FATAL_ERROR "exit.json exit_code ${_code} status ${_status}, timing_valid_for_headline ${_valid}")
endif()
message(STATUS "PASS: exit 4 with exit.json contaminated_host (helper on ${_helper_mask}); other processes on the run's core: ${_core}% of one core "
	"(CPU 0 ${_mask_other}%, its SMT sibling ${_sibling}%; limit ${_limit}%)")
