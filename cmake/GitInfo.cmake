# GitInfo.cmake - the harness commit at BUILD time (M7; M3 follow-up: manifests recorded the configure-time SHA).
#   cmake -DPT_SOURCE_DIR=<repo> -DPT_OUT_HEADER=<build>/Generated/PTGitInfo.h [-DGIT_EXECUTABLE=<git>] -P GitInfo.cmake
# Writes PT_GIT_SHA / PT_GIT_DIRTY / PT_GIT_SHA_SOURCE only when the content changes (restat: an unchanged commit does not
# recompile anything). The build re-runs this script when .git/HEAD, .git/index or .git/logs/HEAD change (a commit, a
# checkout, git add, or a git status that refreshes the index), so the recorded SHA follows every commit without a CMake
# configure. Read-only git calls (--no-optional-locks). "dirty" is the index / work-tree state at that moment: an edit that
# git has not looked at since is caught by the next build after any git command, not by the build itself.
if(NOT DEFINED PT_SOURCE_DIR OR NOT DEFINED PT_OUT_HEADER)
	message(FATAL_ERROR "GitInfo.cmake: -DPT_SOURCE_DIR and -DPT_OUT_HEADER are required")
endif()
if(NOT GIT_EXECUTABLE)
	find_program(GIT_EXECUTABLE git)
endif()
set(_sha "")
set(_dirty 0)
if(GIT_EXECUTABLE AND EXISTS "${PT_SOURCE_DIR}/.git")
	execute_process(COMMAND "${GIT_EXECUTABLE}" --no-optional-locks -C "${PT_SOURCE_DIR}" rev-parse HEAD
		OUTPUT_VARIABLE _sha OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _rc)
	if(NOT _rc EQUAL 0)
		set(_sha "")
	endif()
	execute_process(COMMAND "${GIT_EXECUTABLE}" --no-optional-locks -C "${PT_SOURCE_DIR}" status --porcelain --untracked-files=no
		OUTPUT_VARIABLE _status OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
	if(NOT _status STREQUAL "")
		set(_dirty 1)
	endif()
endif()
set(_text "// PTGitInfo.h - GENERATED at build time by cmake/GitInfo.cmake. Do not edit.
#pragma once
#define PT_GIT_SHA        \"${_sha}\"
#define PT_GIT_DIRTY      ${_dirty}
#define PT_GIT_SHA_SOURCE \"build\"
#define PT_GIT_NOTE       \"recorded at build time (regenerated when .git/HEAD, .git/index or .git/logs/HEAD change); dirty = git status at that moment\"
")
set(_tmp "${PT_OUT_HEADER}.tmp")
file(WRITE "${_tmp}" "${_text}")
file(COPY_FILE "${_tmp}" "${PT_OUT_HEADER}" ONLY_IF_DIFFERENT)
file(REMOVE "${_tmp}")
