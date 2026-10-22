# DumpBuildFlags.cmake - build self-report, G0 source 1 (02_Architecture 4.6, DL49, 03 M0 task 5).
#
# One file, two roles:
#   include(DumpBuildFlags)
#     pt_add_build_flags_report()  build-time add_custom_command(OUTPUT BuildFlags.json Generated/BuildFlagsSha.h
#                                  DEPENDS compile_commands.json). compile_commands.json is written at GENERATE
#                                  time, so a configure-time script could never read it.
#     pt_configure_build_info()    Generated/PTBuildInfo.h (configure-time facts) + the PT::BuildInfo target.
#   cmake -DPT_IN=... -P DumpBuildFlags.cmake
#     The dump itself: parses every TU of compile_commands.json (SDK TUs included), records the effective /arch,
#     /fp, CRT, /std, optimisation, _ITERATOR_DEBUG_LEVEL and selected defines, derives the PhysX configuration from
#     PhysX's own TUs and checks the flag-profile expectations. Violations are listed in BuildFlags.json; the CTest
#     PhysicsTest.Probe.BuildFlags fails when there is any.
#     Both outputs are rewritten only when their content changes. compile_commands.json is rewritten on every CMake
#     generate, so a reconfigure re-runs this script; with unchanged flags the outputs keep their timestamps and the
#     Ninja edge (restat = 1) does not trigger the TUs that include BuildFlagsSha.h.
include_guard(GLOBAL)

if(NOT CMAKE_SCRIPT_MODE_FILE)
	function(pt_add_build_flags_report)
		set(_json   "${CMAKE_BINARY_DIR}/BuildFlags.json")
		set(_header "${CMAKE_BINARY_DIR}/Generated/BuildFlagsSha.h")
		set(_ccdb   "${CMAKE_BINARY_DIR}/compile_commands.json")
		add_custom_command(
			OUTPUT "${_json}" "${_header}"
			COMMAND "${CMAKE_COMMAND}"
				"-DPT_IN=${_ccdb}"
				"-DPT_OUT_JSON=${_json}"
				"-DPT_OUT_HEADER=${_header}"
				"-DPT_BINARY_DIR=${CMAKE_BINARY_DIR}"
				"-DPT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
				"-DPT_DEPS_DIR=${FETCHCONTENT_BASE_DIR}"
				"-DPT_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
				"-DPT_PRESET=${PT_PRESET}"
				"-DPT_FLAG_PROFILE=${PT_FLAG_PROFILE}"
				"-DPT_ARCH_JOLT=${PT_ARCH_JOLT}"
				"-DPT_ARCH_PHYSX=${PT_ARCH_PHYSX}"
				"-DPT_ARCH_BULLET=${PT_ARCH_BULLET}"
				-P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
			DEPENDS "${_ccdb}" "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
			COMMENT "Generating BuildFlags.json from compile_commands.json"
			VERBATIM)
		add_custom_target(PtBuildFlags ALL DEPENDS "${_json}" "${_header}")
		set_property(TARGET PtBuildFlags PROPERTY FOLDER "PhysicsTest/Build")
	endfunction()

	function(pt_configure_build_info)
		foreach(_opt WITH_JOLT WITH_BULLET WITH_PHYSX WITH_CHAOS WITH_VIEWER WITH_PROFILER)
			if(PT_${_opt})
				set(PT_BUILD_${_opt} 1)
			else()
				set(PT_BUILD_${_opt} 0)
			endif()
		endforeach()
		if(PT_PHYSX_USE_BOOTSTRAP)
			set(PT_BUILD_PHYSX_PATH "PhysXBootstrap")
		else()
			set(PT_BUILD_PHYSX_PATH "FetchContent")
		endif()
		if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
			set(PT_BUILD_LTO 1)
		else()
			set(PT_BUILD_LTO 0)
		endif()
		# The harness commit for run manifests (M3): read-only git calls (no optional locks), configure-time values.
		set(PT_BUILD_GIT_SHA "")
		set(PT_BUILD_GIT_DIRTY 0)
		if(GIT_EXECUTABLE AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
			execute_process(COMMAND "${GIT_EXECUTABLE}" --no-optional-locks -C "${CMAKE_SOURCE_DIR}" rev-parse HEAD
				OUTPUT_VARIABLE PT_BUILD_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _git_rc)
			if(NOT _git_rc EQUAL 0)
				set(PT_BUILD_GIT_SHA "")
			endif()
			execute_process(COMMAND "${GIT_EXECUTABLE}" --no-optional-locks -C "${CMAKE_SOURCE_DIR}" status --porcelain --untracked-files=no
				OUTPUT_VARIABLE _git_status OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
			if(NOT _git_status STREQUAL "")
				set(PT_BUILD_GIT_DIRTY 1)
			endif()
		endif()
		configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/PTBuildInfo.h.in" "${CMAKE_BINARY_DIR}/Generated/PTBuildInfo.h" @ONLY)
		# M7: the harness commit at build time (Generated/PTGitInfo.h, cmake/GitInfo.cmake), re-generated when git's HEAD, index
		# or HEAD log change; the bench manifest records it (the configure-time PT_BUILD_GIT_SHA above stays for the viewer).
		set(_git_header "${CMAKE_BINARY_DIR}/Generated/PTGitInfo.h")
		set(_git_depends "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GitInfo.cmake")
		foreach(_git_file HEAD index logs/HEAD)
			if(EXISTS "${CMAKE_SOURCE_DIR}/.git/${_git_file}")
				list(APPEND _git_depends "${CMAKE_SOURCE_DIR}/.git/${_git_file}")
			endif()
		endforeach()
		add_custom_command(
			OUTPUT "${_git_header}"
			COMMAND "${CMAKE_COMMAND}" "-DPT_SOURCE_DIR=${CMAKE_SOURCE_DIR}" "-DPT_OUT_HEADER=${_git_header}" "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
				-P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GitInfo.cmake"
			DEPENDS ${_git_depends}
			COMMENT "Recording the harness commit (PTGitInfo.h)"
			VERBATIM)
		add_custom_target(PtGitInfo ALL DEPENDS "${_git_header}")
		set_property(TARGET PtGitInfo PROPERTY FOLDER "PhysicsTest/Build")
		add_library(pt_build_info INTERFACE)
		add_library(PT::BuildInfo ALIAS pt_build_info)
		target_include_directories(pt_build_info INTERFACE "${CMAKE_BINARY_DIR}/Generated")
		add_dependencies(pt_build_info PtBuildFlags PtGitInfo)   # followed transitively by consumers (CMake >= 3.19)
	endfunction()
	return()
endif()

# =================================================================================================================
# Script mode
# =================================================================================================================
foreach(_req PT_IN PT_OUT_JSON PT_OUT_HEADER PT_BINARY_DIR PT_SOURCE_DIR PT_DEPS_DIR PT_BUILD_TYPE)
	if(NOT DEFINED ${_req})
		message(FATAL_ERROR "DumpBuildFlags.cmake: -D${_req}=... is required")
	endif()
endforeach()
if(NOT EXISTS "${PT_IN}")
	message(FATAL_ERROR "DumpBuildFlags.cmake: ${PT_IN} does not exist (CMAKE_EXPORT_COMPILE_COMMANDS must be ON)")
endif()

# Expected values for the active flag profile (PTFlagProfiles.cmake).
if(PT_BUILD_TYPE STREQUAL "Debug")
	set(_exp_crt "MDd")
else()
	set(_exp_crt "MD")
endif()
set(_exp_fp "fast")
set(_exp_arch_sdk_jolt       "${PT_ARCH_JOLT}")
set(_exp_arch_jolt_consumer  "${PT_ARCH_JOLT}")
set(_exp_arch_sdk_physx      "${PT_ARCH_PHYSX}")
set(_exp_arch_sdk_bullet     "${PT_ARCH_BULLET}")
set(_exp_arch_harness        "")
set(_exp_arch_deps_imgui     "")
set(_exp_arch_other          "")
set(_exp_std_sdk_physx       "c++14")
set(_exp_std_harness         "c++20")
set(_exp_std_harness_c       "c11")     # harness C TUs: the ABI header compiled as C11 (PtAbiC11Check, M2)
# One iterator debug level for every TU (LNK2038 / ODR otherwise): the MSVC STL default for the CRT (2 with _DEBUG).
if(PT_BUILD_TYPE STREQUAL "Debug")
	set(_exp_idl 2)
else()
	set(_exp_idl 0)
endif()

# Path forms as they appear in compile_commands.json (forward slashes, or JSON-escaped backslashes).
string(REPLACE "/" "\\\\" _bin_bs "${PT_BINARY_DIR}")
set(_deps "${PT_DEPS_DIR}")
set(_src  "${PT_SOURCE_DIR}")

set(_groups sdk_jolt sdk_bullet sdk_physx deps_imgui harness other)
foreach(_g IN LISTS _groups)
	set(_n_${_g} 0)
	foreach(_k arch fp crt std idl)
		set(_h_${_g}_${_k} "")
	endforeach()
endforeach()
set(_tu_lines "")
set(_violations "")
# Targets whose facts the generated header carries for the bench's G0 preflight (M7): the backend adapters, the Null DLL and
# the bench exe itself (a harness TU: never /arch).
set(_g0_targets PtBackendNull PtBackend_Null PtBackendJolt PtBackendBullet PtBackendPhysX PhysicsTestBench)
foreach(_t IN LISTS _g0_targets)
	set(_g0_n_${_t} 0)
	foreach(_k arch fp crt idl exp)
		set(_g0_${_t}_${_k} "")
	endforeach()
endforeach()
set(_physx_configs "")
set(_jolt_defines "")
set(_bullet_sdk_signatures "")
set(_bullet_consumers "")
set(_skipped "")
set(_tu_count 0)

# Extract the values of every "--key value" form we care about from one padded command string.
# Matches stop before the next space, so adjacent flags never consume each other's leading space.
function(_pt_extract Cmd Regex Strip OutVar)
	string(REGEX MATCHALL "${Regex}" _m "${Cmd}")
	set(_vals "")
	foreach(_x IN LISTS _m)
		string(REGEX REPLACE "${Strip}" "" _x "${_x}")
		list(APPEND _vals "${_x}")
	endforeach()
	set(${OutVar} "${_vals}" PARENT_SCOPE)
endfunction()

function(_pt_json_array OutVar)
	set(_s "")
	foreach(_x IN LISTS ARGN)
		if(_s)
			string(APPEND _s ",")
		endif()
		string(APPEND _s "\"${_x}\"")
	endforeach()
	set(${OutVar} "[${_s}]" PARENT_SCOPE)
endfunction()

macro(_pt_process_tu)
	math(EXPR _tu_count "${_tu_count} + 1")

	# Group by source location (plain prefix comparison: paths may contain regex metacharacters).
	set(_group other)
	# *-build covers generated SDK TUs such as Jolt's precompiled-header TU (jolt-build/.../cmake_pch.cxx).
	foreach(_pair "jolt-src=sdk_jolt" "jolt-build=sdk_jolt" "bullet3-src=sdk_bullet" "bullet3-build=sdk_bullet"
			"physx-src=sdk_physx" "physx-build=sdk_physx" "imgui-src=deps_imgui" "implot-src=deps_imgui")
		string(REPLACE "=" ";" _pair "${_pair}")
		list(GET _pair 0 _dir)
		list(GET _pair 1 _grp)
		string(FIND "${_file_n}" "${_deps}/${_dir}/" _pos)
		if(_pos EQUAL 0)
			set(_group ${_grp})
			break()
		endif()
	endforeach()
	# Harness: Source/ and TUs generated into the build tree's Generated/ directory (e.g. the M2 backend registry).
	foreach(_harness_root "${_src}/Source/" "${PT_BINARY_DIR}/Generated/")
		string(FIND "${_file_n}" "${_harness_root}" _pos)
		if(_group STREQUAL "other" AND _pos EQUAL 0)
			set(_group harness)
		endif()
	endforeach()
	# Most specific first: _deps and the build tree live under the source tree (out/<preset>). <BIN> keeps generated
	# TUs comparable between two build trees (Tools/CompareBuildFlags.cmake keys TUs by this path).
	string(REPLACE "${_deps}" "<DEPS>" _file_rel "${_file_n}")
	string(REPLACE "${PT_BINARY_DIR}" "<BIN>" _file_rel "${_file_rel}")
	string(REPLACE "${_src}" "<SRC>" _file_rel "${_file_rel}")
	set(_target "")
	if(_output_n MATCHES "CMakeFiles/([^/]+)\\.dir/")
		set(_target "${CMAKE_MATCH_1}")
	endif()

	set(_padded " ${_cmd} ")
	_pt_extract("${_padded}" " [/-]arch:[^ ]+"           "^ [/-]arch:" _arch_all)
	_pt_extract("${_padded}" " [/-]fp:(fast|precise|strict)" "^ [/-]fp:"   _fp_all)
	_pt_extract("${_padded}" " [/-]M[DT]d?[^ ]*"          "^ [/-]"      _crt_raw)
	_pt_extract("${_padded}" " [/-]std:[^ ]+"            "^ [/-]std:"  _std_all)
	_pt_extract("${_padded}" " [/-]O[d12x][^ ]*"          "^ [/-]"      _opt_all)
	_pt_extract("${_padded}" " [/-]GL[^ ]*"               "^ [/-]"      _gl_all)
	# Defines in command-line order, all three spellings: -DNAME[=V], /D "NAME[=V]" and /D NAME[=V]. PhysX passes
	# NDEBUG, _DEBUG, _ITERATOR_DEBUG_LEVEL and _HAS_ITERATOR_DEBUGGING in the quoted, space-separated form
	# (physx/source/compiler/cmake/modules/NvidiaBuildOptions.cmake:58); in the JSON text those quotes appear as \".
	string(REGEX MATCHALL [=[ [/-]D( \\"[^"\\]*\\"| [^ "\\/-][^ ]*|[^ ]+)]=] _def_raw "${_padded}")
	set(_def_all "")
	foreach(_d IN LISTS _def_raw)
		if(_d MATCHES [=[^ [/-]D \\"(.*)\\"$]=])
			list(APPEND _def_all "${CMAKE_MATCH_1}")
		else()
			string(REGEX REPLACE "^ [/-]D ?" "" _d "${_d}")
			list(APPEND _def_all "${_d}")
		endif()
	endforeach()
	set(_crt_all "")
	foreach(_c IN LISTS _crt_raw)
		if(_c MATCHES "^M[DT]d?$")
			list(APPEND _crt_all "${_c}")
		endif()
	endforeach()
	set(_lto 0)
	if("GL" IN_LIST _gl_all)
		set(_lto 1)
	endif()
	# MSVC: the last occurrence of an option wins.
	foreach(_k arch fp crt std opt)
		set(_${_k} "")
		if(_${_k}_all)
			list(GET _${_k}_all -1 _${_k})
		endif()
	endforeach()
	set(_defs "")
	foreach(_d IN LISTS _def_all)
		if(_d MATCHES "^(JPH_|PX_|BT_|PT_|NDEBUG|_DEBUG|_ITERATOR_DEBUG_LEVEL|_HAS_ITERATOR_DEBUGGING|_SECURE_SCL|DISABLE_CUDA_PHYSX|IMGUI_)"
				AND NOT _d MATCHES "\\\\$")
			list(APPEND _defs "${_d}")
		endif()
	endforeach()

	# Effective _ITERATOR_DEBUG_LEVEL, derived as the MSVC STL does (MSVC 14.44 include/yvals.h, sections A and B):
	# an explicit value wins; otherwise _HAS_ITERATOR_DEBUGGING (default 1 with _DEBUG, which /MDd and /MTd imply)
	# gives 2, else _SECURE_SCL (default = _HAS_ITERATOR_DEBUGGING) gives 1, else 0. The last definition wins.
	set(_idl "")
	set(_hid "")
	set(_scl "")
	set(_debug_macro 0)
	if(_crt MATCHES "d$")
		set(_debug_macro 1)
	endif()
	foreach(_d IN LISTS _def_all)
		if(_d MATCHES "^_ITERATOR_DEBUG_LEVEL=([0-9]+)$")
			set(_idl "${CMAKE_MATCH_1}")
		elseif(_d MATCHES "^_HAS_ITERATOR_DEBUGGING=([0-9]+)$")
			set(_hid "${CMAKE_MATCH_1}")
		elseif(_d MATCHES "^_SECURE_SCL=([0-9]+)$")
			set(_scl "${CMAKE_MATCH_1}")
		elseif(_d MATCHES "^_DEBUG(=.*)?$")
			set(_debug_macro 1)
		endif()
	endforeach()
	if(_idl STREQUAL "")
		if(_hid STREQUAL "")
			set(_hid ${_debug_macro})
		endif()
		if(_scl STREQUAL "")
			set(_scl ${_hid})
		endif()
		if(NOT _hid EQUAL 0)
			set(_idl 2)
		elseif(NOT _scl EQUAL 0)
			set(_idl 1)
		else()
			set(_idl 0)
		endif()
	endif()

	string(FIND "${_cmd}" "jolt-src" _jolt_pos)
	if(_jolt_pos GREATER -1)
		set(_links_jolt 1)
	else()
		set(_links_jolt 0)
	endif()
	# PhysX / Bullet consumers (M7, MatchedAVX2): a harness-side TU compiled against PhysX or Bullet (the backend adapters, the
	# SDK smoke programs) carries that SDK's /arch (PUBLIC on the SDK targets, PTFlagProfiles.cmake).
	string(FIND "${_cmd}" "physx-src" _physx_pos)
	string(FIND "${_cmd}" "bullet3-src" _bullet_pos)

	# Normalised command hash: identical flags in two build trees (e.g. PhysX on/off) give identical hashes.
	set(_norm "${_cmd}")
	_pt_restore(_norm)
	string(REPLACE "${PT_BINARY_DIR}" "<BIN>" _norm "${_norm}")
	string(REPLACE "${_bin_bs}" "<BIN>" _norm "${_norm}")
	string(SHA256 _cmd_sha "${_norm}")

	# Expectations.
	if(_group STREQUAL "harness" AND _links_jolt)
		set(_exp_arch "${_exp_arch_jolt_consumer}")
	elseif(_group STREQUAL "harness" AND _physx_pos GREATER -1)
		set(_exp_arch "${_exp_arch_sdk_physx}")
	elseif(_group STREQUAL "harness" AND _bullet_pos GREATER -1)
		set(_exp_arch "${_exp_arch_sdk_bullet}")
	else()
		set(_exp_arch "${_exp_arch_${_group}}")
	endif()
	if(NOT _arch STREQUAL _exp_arch)
		list(APPEND _violations "${_file_rel}: /arch '${_arch}' (expected '${_exp_arch}')")
	endif()
	list(REMOVE_DUPLICATES _arch_all)
	list(LENGTH _arch_all _n_arch)
	if(_n_arch GREATER 1)
		list(APPEND _violations "${_file_rel}: conflicting /arch flags ${_arch_all}")
	endif()
	if(NOT _fp STREQUAL _exp_fp)
		list(APPEND _violations "${_file_rel}: /fp '${_fp}' (expected '${_exp_fp}')")
	endif()
	foreach(_f IN LISTS _fp_all)
		if(NOT _f STREQUAL _exp_fp)
			list(APPEND _violations "${_file_rel}: conflicting /fp:${_f}")
		endif()
	endforeach()
	if(NOT _crt STREQUAL _exp_crt)
		list(APPEND _violations "${_file_rel}: CRT '${_crt}' (expected '${_exp_crt}')")
	endif()
	foreach(_c IN LISTS _crt_all)
		if(_c MATCHES "^MT")
			list(APPEND _violations "${_file_rel}: static CRT -${_c}")
		endif()
	endforeach()
	set(_exp_std "")
	if(DEFINED _exp_std_${_group})
		set(_exp_std "${_exp_std_${_group}}")
	endif()
	if(_group STREQUAL "harness" AND _file_n MATCHES "\\.c$")
		set(_exp_std "${_exp_std_harness_c}")
	endif()
	if(NOT _exp_std STREQUAL "" AND NOT _std STREQUAL _exp_std)
		list(APPEND _violations "${_file_rel}: /std '${_std}' (expected '${_exp_std}')")
	endif()
	if(_lto)
		list(APPEND _violations "${_file_rel}: /GL (LTO is off for every engine)")
	endif()
	if(NOT _idl EQUAL _exp_idl)
		list(APPEND _violations "${_file_rel}: _ITERATOR_DEBUG_LEVEL ${_idl} (expected ${_exp_idl}: mixed levels risk LNK2038 / ODR)")
	endif()

	# PhysX configuration as seen by PhysX's own TUs (physx/source/compiler/cmake/windows/PhysX.cmake:94-101).
	if(_group STREQUAL "sdk_physx")
		if("PX_DEBUG=1" IN_LIST _defs)
			list(APPEND _physx_configs debug)
		elseif("PX_PROFILE=1" IN_LIST _defs)
			list(APPEND _physx_configs profile)
		elseif("PX_CHECKED=1" IN_LIST _defs)
			list(APPEND _physx_configs checked)
		elseif("PX_SUPPORT_PVD=0" IN_LIST _defs)
			list(APPEND _physx_configs release)
		else()
			list(APPEND _physx_configs unknown)
		endif()
	endif()
	if(_group STREQUAL "sdk_jolt")
		foreach(_d IN LISTS _defs)
			if(_d MATCHES "^JPH_")
				list(APPEND _jolt_defines "${_d}")
			endif()
		endforeach()
	endif()
	# Header-visible Bullet definitions (BT_*): Bullet's TUs and every harness TU that compiles against Bullet must agree
	# (inline code in Bullet headers, e.g. btRigidBody.h with BT_CLAMP_VELOCITY_TO; cmake/ThirdParty/PtBullet.cmake).
	string(FIND "${_cmd}" "bullet3-src" _bullet_pos)
	if(_group STREQUAL "sdk_bullet" OR (_group STREQUAL "harness" AND _bullet_pos GREATER -1))
		set(_bt_defs "")
		foreach(_d IN LISTS _defs)
			if(_d MATCHES "^BT_")
				list(APPEND _bt_defs "${_d}")
			endif()
		endforeach()
		list(REMOVE_DUPLICATES _bt_defs)
		list(SORT _bt_defs)
		string(JOIN "," _bt_signature ${_bt_defs})
		if(_bt_signature STREQUAL "")
			set(_bt_signature "<none>")
		endif()
		if(_group STREQUAL "sdk_bullet")
			list(APPEND _bullet_sdk_signatures "${_bt_signature}")
		else()
			list(APPEND _bullet_consumers "${_file_rel}|${_bt_signature}")
		endif()
	endif()

	# G0 facts per target (M7): the bench's preflight compares a backend adapter's runtime self-report (PtBuildReport.h)
	# with what compile_commands.json says about the same adapter target, and both with the flag profile.
	if(_target IN_LIST _g0_targets)
		math(EXPR _g0_n_${_target} "${_g0_n_${_target}} + 1")
		foreach(_k arch fp crt idl)
			set(_v "${_${_k}}")
			if(_v STREQUAL "")
				set(_v "none")
			endif()
			if(NOT _v IN_LIST _g0_${_target}_${_k})
				list(APPEND _g0_${_target}_${_k} "${_v}")
			endif()
		endforeach()
		set(_v "${_exp_arch}")
		if(_v STREQUAL "")
			set(_v "none")
		endif()
		if(NOT _v IN_LIST _g0_${_target}_exp)
			list(APPEND _g0_${_target}_exp "${_v}")
		endif()
	endif()

	# Histograms.
	math(EXPR _n_${_group} "${_n_${_group}} + 1")
	foreach(_k arch fp crt std idl)
		set(_v "${_${_k}}")
		if(_v STREQUAL "")
			set(_v "none")
		endif()
		list(APPEND _h_${_group}_${_k} "${_v}")
	endforeach()

	string(REPLACE "_" "." _group_name "${_group}")
	_pt_json_array(_defs_json ${_defs})
	string(APPEND _tu_lines
		"{\"group\":\"${_group_name}\",\"target\":\"${_target}\",\"file\":\"${_file_rel}\",\"links_jolt\":${_links_jolt},"
		"\"arch\":\"${_arch}\",\"fp\":\"${_fp}\",\"crt\":\"${_crt}\",\"std\":\"${_std}\",\"opt\":\"${_opt}\",\"lto\":${_lto},"
		"\"idl\":${_idl},\"defines\":${_defs_json},\"cmd_sha256\":\"${_cmd_sha}\"},\n")
endmacro()

# Read as one string and split on newlines here: file(STRINGS) returns a CMake list, so a ';' inside a command
# would split it and an unbalanced '[' would merge lines. ';', '[' and ']' become placeholders. "file"/"output" are
# restored at once; the command keeps them during flag extraction (extracted values go through list operations) and
# is restored only for its hash (cmd_sha256), so a define such as /D "A=x;y" shows up as A=x<PT_SEMI>y.
file(READ "${PT_IN}" _ccdb)
string(REPLACE ";" "<PT_SEMI>" _ccdb "${_ccdb}")
string(REPLACE "[" "<PT_LB>" _ccdb "${_ccdb}")
string(REPLACE "]" "<PT_RB>" _ccdb "${_ccdb}")
string(REPLACE "\r" "" _ccdb "${_ccdb}")
string(REPLACE "\n" ";" _lines "${_ccdb}")
unset(_ccdb)
macro(_pt_restore Var)
	string(REPLACE "<PT_SEMI>" ";" ${Var} "${${Var}}")
	string(REPLACE "<PT_LB>" "[" ${Var} "${${Var}}")
	string(REPLACE "<PT_RB>" "]" ${Var} "${${Var}}")
endmacro()
set(_cmd "")
set(_file "")
set(_output "")
foreach(_line IN LISTS _lines)
	if(_line MATCHES "^[ \t]*\"command\": \"(.*)\",?$")
		set(_cmd "${CMAKE_MATCH_1}")
	elseif(_line MATCHES "^[ \t]*\"file\": \"(.*)\",?$")
		set(_file "${CMAKE_MATCH_1}")
		_pt_restore(_file)
	elseif(_line MATCHES "^[ \t]*\"output\": \"(.*)\",?$")
		set(_output "${CMAKE_MATCH_1}")
		_pt_restore(_output)
		string(REPLACE "\\\\" "/" _file_n "${_file}")
		string(REPLACE "\\\\" "/" _output_n "${_output}")
		if(_file_n MATCHES "\\.(c|cc|cpp|cxx)$")
			_pt_process_tu()
		else()
			# Resource scripts (PhysX *.rc via rc.exe) and other non-C/C++ entries carry no compiler flags.
			list(APPEND _skipped "${_file_n}")
		endif()
	endif()
endforeach()
if(_tu_count EQUAL 0)
	message(FATAL_ERROR "DumpBuildFlags.cmake: no translation units found in ${PT_IN}")
endif()

# Summary per group: {"tu": n, "arch": {"AVX2": n, ...}, ...}
set(_summary "")
foreach(_g IN LISTS _groups)
	if(_n_${_g} EQUAL 0)
		continue()
	endif()
	string(REPLACE "_" "." _gname "${_g}")
	set(_gjson "\"tu\":${_n_${_g}}")
	foreach(_k arch fp crt std idl)
		set(_all ${_h_${_g}_${_k}})
		set(_distinct ${_all})
		list(REMOVE_DUPLICATES _distinct)
		set(_kjson "")
		foreach(_v IN LISTS _distinct)
			set(_cnt 0)
			foreach(_x IN LISTS _all)
				if(_x STREQUAL _v)
					math(EXPR _cnt "${_cnt} + 1")
				endif()
			endforeach()
			if(_kjson)
				string(APPEND _kjson ",")
			endif()
			string(APPEND _kjson "\"${_v}\":${_cnt}")
		endforeach()
		string(APPEND _gjson ",\"${_k}\":{${_kjson}}")
	endforeach()
	if(_summary)
		string(APPEND _summary ",\n")
	endif()
	string(APPEND _summary "    \"${_gname}\": {${_gjson}}")
endforeach()

list(REMOVE_DUPLICATES _physx_configs)
list(REMOVE_DUPLICATES _jolt_defines)
list(SORT _jolt_defines)
set(_bullet_signature "")
if(_bullet_sdk_signatures)
	list(REMOVE_DUPLICATES _bullet_sdk_signatures)
	list(LENGTH _bullet_sdk_signatures _n_bullet_signatures)
	if(_n_bullet_signatures GREATER 1)
		string(REPLACE ";" " | " _bullet_signatures_text "${_bullet_sdk_signatures}")
		list(APPEND _violations "Bullet TUs disagree on their BT_* definitions: ${_bullet_signatures_text}")
	endif()
	list(GET _bullet_sdk_signatures 0 _bullet_signature)
	foreach(_c IN LISTS _bullet_consumers)
		string(REGEX MATCH "^([^|]*)[|](.*)$" _m "${_c}")
		if(NOT CMAKE_MATCH_2 STREQUAL _bullet_signature)
			list(APPEND _violations "${CMAKE_MATCH_1}: BT_* definitions '${CMAKE_MATCH_2}' differ from Bullet's '${_bullet_signature}' (inline-code ODR)")
		endif()
	endforeach()
endif()
set(_bullet_defines_json "[]")
if(_bullet_signature AND NOT _bullet_signature STREQUAL "<none>")
	string(REPLACE "," ";" _bullet_list "${_bullet_signature}")
	_pt_json_array(_bullet_defines_json ${_bullet_list})
endif()
list(LENGTH _violations _violation_count)
list(LENGTH _skipped _skipped_count)
_pt_json_array(_physx_json ${_physx_configs})
_pt_json_array(_jolt_json ${_jolt_defines})
set(_viol_lines "")
foreach(_v IN LISTS _violations)
	string(REPLACE "\"" "'" _v "${_v}")
	if(_viol_lines)
		string(APPEND _viol_lines ",\n")
	endif()
	string(APPEND _viol_lines "    \"${_v}\"")
endforeach()
string(REGEX REPLACE ",\n$" "\n" _tu_lines "${_tu_lines}")

# Writes Content to Path only when it differs from the current file (see the header of this file: restat).
function(_pt_write_if_different Path Content)
	set(_tmp "${Path}.tmp")
	file(WRITE "${_tmp}" "${Content}")
	file(COPY_FILE "${_tmp}" "${Path}" ONLY_IF_DIFFERENT)
	file(REMOVE "${_tmp}")
endfunction()

# No timestamp in the file: its SHA-256 (embedded through BuildFlagsSha.h) identifies the flag set itself.
set(_json_text
"{
  \"schema\": \"PhysicsTest.BuildFlags/1\",
  \"preset\": \"${PT_PRESET}\",
  \"configuration\": \"${PT_BUILD_TYPE}\",
  \"flag_profile\": \"${PT_FLAG_PROFILE}\",
  \"source\": \"<BIN>/compile_commands.json\",
  \"tu_count\": ${_tu_count},
  \"skipped_non_cxx\": ${_skipped_count},
  \"physx_config\": ${_physx_json},
  \"jolt_public_defines\": ${_jolt_json},
  \"bullet_header_defines\": ${_bullet_defines_json},
  \"expect\": {\"crt\": \"${_exp_crt}\", \"fp\": \"${_exp_fp}\", \"idl\": ${_exp_idl}, \"arch\": {\"sdk.jolt\": \"${_exp_arch_sdk_jolt}\", \"jolt_consumers\": \"${_exp_arch_jolt_consumer}\", \"sdk.physx\": \"${_exp_arch_sdk_physx}\", \"sdk.bullet\": \"${_exp_arch_sdk_bullet}\", \"harness\": \"\"}, \"std\": {\"sdk.physx\": \"${_exp_std_sdk_physx}\", \"harness\": \"${_exp_std_harness}\", \"harness_c\": \"${_exp_std_harness_c}\"}},
  \"summary\": {
${_summary}
  },
  \"violation_count\": ${_violation_count},
  \"violations\": [
${_viol_lines}
  ],
  \"tus\": [
${_tu_lines}  ]
}
")
_pt_write_if_different("${PT_OUT_JSON}" "${_json_text}")
file(SHA256 "${PT_OUT_JSON}" _json_sha)

# G0 facts (M7): one compact JSON object, embedded as a string literal so that the bench needs no file at run time.
#   {"flag_profile":..,"configuration":..,"violations":n,"physx_config":[..],
#    "groups":{"sdk.jolt":{"arch":[..],"fp":[..],"crt":[..],"idl":[..],"expect_arch":".."},..},
#    "targets":{"PtBackendJolt":{"tu":n,"arch":[..],"fp":[..],"crt":[..],"idl":[..],"expect_arch":[..]},..}}
set(_g0_groups "")
foreach(_g sdk_jolt sdk_bullet sdk_physx harness)
	if(_n_${_g} EQUAL 0)
		continue()
	endif()
	string(REPLACE "_" "." _gname "${_g}")
	set(_gjson "\"tu\":${_n_${_g}}")
	foreach(_k arch fp crt idl)
		set(_distinct ${_h_${_g}_${_k}})
		list(REMOVE_DUPLICATES _distinct)
		_pt_json_array(_arr ${_distinct})
		string(APPEND _gjson ",\"${_k}\":${_arr}")
	endforeach()
	if(_g STREQUAL "harness")
		set(_ea "${_exp_arch_harness}")
	else()
		set(_ea "${_exp_arch_${_g}}")
	endif()
	if(_ea STREQUAL "")
		set(_ea "none")
	endif()
	string(APPEND _gjson ",\"expect_arch\":\"${_ea}\"")
	if(_g0_groups)
		string(APPEND _g0_groups ",")
	endif()
	string(APPEND _g0_groups "\"${_gname}\":{${_gjson}}")
endforeach()
set(_g0_target_json "")
foreach(_t IN LISTS _g0_targets)
	if(_g0_n_${_t} EQUAL 0)
		continue()
	endif()
	set(_tj "\"tu\":${_g0_n_${_t}}")
	foreach(_k arch fp crt idl)
		_pt_json_array(_arr ${_g0_${_t}_${_k}})
		string(APPEND _tj ",\"${_k}\":${_arr}")
	endforeach()
	_pt_json_array(_arr ${_g0_${_t}_exp})
	string(APPEND _tj ",\"expect_arch\":${_arr}")
	if(_g0_target_json)
		string(APPEND _g0_target_json ",")
	endif()
	string(APPEND _g0_target_json "\"${_t}\":{${_tj}}")
endforeach()
set(_g0_json "{\"flag_profile\":\"${PT_FLAG_PROFILE}\",\"configuration\":\"${PT_BUILD_TYPE}\",\"violations\":${_violation_count},\"physx_config\":${_physx_json},\"jolt_public_defines\":${_jolt_json},\"groups\":{${_g0_groups}},\"targets\":{${_g0_target_json}}}")
string(REPLACE "\\" "\\\\" _g0_literal "${_g0_json}")
string(REPLACE "\"" "\\\"" _g0_literal "${_g0_literal}")

set(_header_text
"// BuildFlagsSha.h - GENERATED at build time by cmake/DumpBuildFlags.cmake. Do not edit.
#pragma once
#define PT_BUILD_FLAGS_SHA256      \"${_json_sha}\"
#define PT_BUILD_FLAGS_TU_COUNT    ${_tu_count}
#define PT_BUILD_FLAGS_VIOLATIONS  ${_violation_count}
// G0 facts of this build tree (M7): per SDK group and per adapter target, the /arch, /fp, CRT and iterator debug level of
// its TUs, and the flag profile's expectation. The bench's preflight compares them with the adapters' runtime self-report.
#define PT_BUILD_FLAGS_G0_JSON     \"${_g0_literal}\"
")
_pt_write_if_different("${PT_OUT_HEADER}" "${_header_text}")
message(STATUS "BuildFlags.json: ${_tu_count} TUs, ${_violation_count} violation(s), PhysX config ${_physx_configs}, sha256 ${_json_sha}")
