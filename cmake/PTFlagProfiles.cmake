# PTFlagProfiles.cmake - build flag profiles (02_Architecture 4.2, DL12).
#
#   Profile      | Jolt SDK + Jolt consumers | PhysX / Bullet SDK + their consumers (adapters) | harness
#   Shipped      | /arch:AVX2 (forced)       | vendor default (SSE2)                         | no /arch
#   MatchedAVX2  | /arch:AVX2                | /arch:AVX2 (PUBLIC on the SDK targets, M7)    | no /arch     <- test-avx2 preset
#
# MatchedAVX2 (M7, 02 4.2, DL12): every SDK and every TU that compiles against an SDK (the backend adapters, the SDK smoke
# programs) is /arch:AVX2, exactly as Jolt is in both profiles; harness TUs never are, so desc_sha256 does not depend on
# the profile. PhysX.cmake / PtBullet.cmake add the option PUBLIC on the SDK targets (like Jolt.cmake), so the adapters,
# which link the SDK PRIVATE, get it and nothing above them does (CTest PhysicsTest.Probe.HarnessSdkIsolation).
# The expectations below are consumed by DumpBuildFlags.cmake, which flags every TU that deviates.
include_guard(GLOBAL)

if(PT_FLAG_PROFILE STREQUAL "Shipped")
	set(PT_ARCH_JOLT   "AVX2")   # Jolt.cmake:969-979 adds /arch only for VS generators; we force it (PUBLIC) for Ninja
	set(PT_ARCH_PHYSX  "")
	set(PT_ARCH_BULLET "")
elseif(PT_FLAG_PROFILE STREQUAL "MatchedAVX2")
	set(PT_ARCH_JOLT   "AVX2")
	set(PT_ARCH_PHYSX  "AVX2")
	set(PT_ARCH_BULLET "AVX2")
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
