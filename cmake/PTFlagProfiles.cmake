# PTFlagProfiles.cmake - build flag profiles (02_Architecture 4.2, DL12).
#
#   Profile      | Jolt SDK + Jolt consumers | PhysX / Bullet SDK + adapters | harness
#   Shipped      | /arch:AVX2 (forced)       | vendor default (SSE2)         | no /arch
#   MatchedAVX2  | /arch:AVX2                | /arch:AVX2 via pt_apply_arch  | no /arch     <- M7 (test-avx2 preset)
#
# The expectations below are consumed by DumpBuildFlags.cmake, which flags every TU that deviates.
include_guard(GLOBAL)

if(PT_FLAG_PROFILE STREQUAL "Shipped")
	set(PT_ARCH_JOLT   "AVX2")   # Jolt.cmake:969-979 adds /arch only for VS generators; we force it (PUBLIC) for Ninja
	set(PT_ARCH_PHYSX  "")
	set(PT_ARCH_BULLET "")
elseif(PT_FLAG_PROFILE STREQUAL "MatchedAVX2")
	message(FATAL_ERROR "PT_FLAG_PROFILE=MatchedAVX2 arrives with the test-avx2 preset in M7 (docs/03_WorkPlan.md).")
else()
	message(FATAL_ERROR "Unknown PT_FLAG_PROFILE='${PT_FLAG_PROFILE}' (expected Shipped or MatchedAVX2)")
endif()
set(PT_ARCH_HARNESS "")          # never: desc_sha256 must be identical across flag profiles (DL12)

#[[
pt_apply_arch(ARCH <AVX|AVX2|AVX512> SCOPE <PUBLIC|PRIVATE|INTERFACE> [TARGETS <t>...] [DIRECTORIES <dir>...])
	Adds /arch:<ARCH> to the listed targets, or to every buildsystem target found recursively under the listed
	source directories (SDK subdirectories, MatchedAVX2 in M7). /arch is never set globally (02 4.1).
	Every touched target is recorded in the global property PT_ARCH_TARGETS.
#]]
function(pt_apply_arch)
	cmake_parse_arguments(PARSE_ARGV 0 Arg "" "ARCH;SCOPE" "TARGETS;DIRECTORIES")
	if(NOT Arg_ARCH OR NOT Arg_SCOPE)
		message(FATAL_ERROR "pt_apply_arch: ARCH and SCOPE are required")
	endif()
	set(_targets ${Arg_TARGETS})
	foreach(_dir IN LISTS Arg_DIRECTORIES)
		_pt_collect_targets_recursive("${_dir}" _targets)
	endforeach()
	foreach(_t IN LISTS _targets)
		get_target_property(_type ${_t} TYPE)
		if(_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
			target_compile_options(${_t} ${Arg_SCOPE} /arch:${Arg_ARCH})
			set_property(GLOBAL APPEND PROPERTY PT_ARCH_TARGETS "${_t}=${Arg_ARCH}")
		endif()
	endforeach()
endfunction()

function(_pt_collect_targets_recursive Dir OutVar)
	get_property(_targets DIRECTORY "${Dir}" PROPERTY BUILDSYSTEM_TARGETS)
	get_property(_subdirs DIRECTORY "${Dir}" PROPERTY SUBDIRECTORIES)
	foreach(_sub IN LISTS _subdirs)
		set(_sub_targets "")
		_pt_collect_targets_recursive("${_sub}" _sub_targets)
		list(APPEND _targets ${_sub_targets})
	endforeach()
	set(${OutVar} ${${OutVar}} ${_targets} PARENT_SCOPE)
endfunction()
