# PTToolchain.cmake - one toolchain policy for every target (02_Architecture 4.1, D3, D5, DL12, DL13).
# Must be included BEFORE any SDK is added: the CRT and /fp:fast below are inherited by SDK directories.
include_guard(GLOBAL)

# --- Guards ---------------------------------------------------------------------------------------------------------
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "PhysicsTest supports MSVC x64 only (found ${CMAKE_CXX_COMPILER_ID}, pointer size ${CMAKE_SIZEOF_VOID_P}). "
		"Run CMake through Tools\\cmake.cmd, which sets up vcvars64.")
endif()

get_property(PT_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(PT_MULTI_CONFIG)
	# VS generators are not supported for bench presets (02 4.4). PhysX FORCE-rewrites the configuration list on
	# multi-config generators (PhysXConfigurationTypes.cmake:12-36), so only the bootstrap path is usable there.
	set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "PhysicsTest configurations" FORCE)
	if(NOT PT_PHYSX_USE_BOOTSTRAP)
		message(WARNING "Multi-config generator '${CMAKE_GENERATOR}': forcing PT_PHYSX_USE_BOOTSTRAP=ON")
		set(PT_PHYSX_USE_BOOTSTRAP ON CACHE BOOL "" FORCE)
	endif()
else()
	if(NOT CMAKE_BUILD_TYPE)
		message(FATAL_ERROR "CMAKE_BUILD_TYPE is empty. Use a preset (dbg | dev | test) or pass -DCMAKE_BUILD_TYPE=Debug|Release.")
	endif()
	if(NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
		# RelWithDebInfo adds /Ob1 and maps to no PhysX config; MinSizeRel is not a UE configuration either.
		message(FATAL_ERROR "CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}' is rejected. Only Debug (dbg) and Release (dev, test) are supported.")
	endif()
endif()

# --- CRT, debug info, LTO, compile database ---------------------------------------------------------------------
# One dynamic CRT for every target, SDKs included (D3). CMP0091 is NEW from the top-level project() call, so this
# abstraction also governs SDK directories that lower their own policy level (Bullet: 3.10).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "MSVC CRT (PhysicsTest: /MD, /MDd)" FORCE)
# /Zi everywhere (CMP0141 NEW); Release links with /DEBUG:FULL and keeps /OPT:REF,ICF.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "ProgramDatabase" CACHE STRING "MSVC debug information format (/Zi)" FORCE)
add_link_options("$<$<CONFIG:Release>:/DEBUG:FULL>" "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
# No LTO for any engine (UE: bAllowLTCG = false).
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
# M10 (02 4.1, optional): reproducible compile and link. /Brepro replaces the timestamps the compiler and the linker embed
# (COFF headers, the PE header, the debug directory) with content hashes, so relinking unchanged code keeps the exe's and a
# DLL backend's SHA-256 and their conformance stamps (02 12.4). Off by default; the stamp key does not depend on it.
if(PT_REPRO_LINK)
	add_compile_options(/Brepro)
	add_link_options(/Brepro)
endif()
# compile_commands.json is the input of BuildFlags.json (G0 source 1, 02 4.6).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Write compile_commands.json (input of BuildFlags.json)" FORCE)

# --- Floating point (D5) --------------------------------------------------------------------------------------------
# Directory-level option: inherited by every subdirectory, SDKs included. /arch is NEVER global (02 4.1).
add_compile_options(/fp:fast)

# --- Output layout --------------------------------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/bin")

# No C++20 modules in the harness (as in UE): skip the per-TU module dependency scan that CMP0155 enables for C++20.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# NOTE: no global CMAKE_CXX_STANDARD. PhysX compiles with /std:c++14 /WX and a global standard breaks it (C5055).
# Harness targets get C++20 per target through pt_target_defaults().

#[[
pt_target_defaults(<Target>)
	Harness-side compile policy (02 4.1): C++20, /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor,
	NOMINMAX WIN32_LEAN_AND_MEAN UNICODE, PT_DO_CHECK. Never applied to SDK or third-party targets. /WX makes a
	compiler warning in a harness TU fail every preset (SDK headers are wrapped by Misc/PtThirdPartyIncludesStart.h).
#]]
function(pt_target_defaults Target)
	get_target_property(_type ${Target} TYPE)
	if(_type STREQUAL "INTERFACE_LIBRARY")
		target_compile_features(${Target} INTERFACE cxx_std_20)
		return()
	endif()
	target_compile_features(${Target} PUBLIC cxx_std_20)
	target_compile_options(${Target} PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor)
	target_compile_definitions(${Target} PRIVATE
		NOMINMAX
		WIN32_LEAN_AND_MEAN
		UNICODE
		_UNICODE
		PT_DO_CHECK=$<IF:$<BOOL:${PT_DO_CHECK}>,1,0>)
endfunction()
