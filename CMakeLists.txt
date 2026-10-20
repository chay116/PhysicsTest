# PhysicsTest - Chaos vs Bullet / PhysX / Jolt comparison harness (Windows, MSVC x64, Ninja).
# Design: docs/02_Architecture.md (section 4 = CMake), milestone contract: docs/03_WorkPlan.md.
#
#   Tools\cmake.cmd --preset dev
#   Tools\cmake.cmd --build --preset dev
#   Tools\ctest.cmd --preset dev
cmake_minimum_required(VERSION 3.25...4.1)   # 3.25: block(), add_subdirectory(... SYSTEM)

project(PhysicsTest
	VERSION 0.1.0
	DESCRIPTION "Chaos vs Bullet / PhysX / Jolt speed and behaviour harness"
	LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThirdParty")

# Order matters: options and pins first, then the toolchain (CRT, /fp:fast) BEFORE any SDK is added (D3).
include(PTOptions)
include(PTToolchain)
include(PTFlagProfiles)
include(PTModules)

# Third-party dependencies (each module creates PT::<Dep> wrapper targets).
include(SmallDeps)
include(ImGui)
if(PT_WITH_JOLT)
	include(Jolt)
endif()
if(PT_WITH_BULLET)
	include(PtBullet)
endif()
if(PT_WITH_PHYSX)
	if(PT_PHYSX_USE_BOOTSTRAP)
		include(PhysXBootstrap)
	else()
		include(PhysX)
	endif()
endif()
if(PT_WITH_CHAOS)
	message(STATUS "PT_WITH_CHAOS=ON: the UBT bridge arrives in M2.5/M13; nothing is built for it yet")
endif()

# Build self-report (G0 source 1): BuildFlags.json is generated at BUILD time from compile_commands.json.
include(DumpBuildFlags)
pt_add_build_flags_report()
pt_configure_build_info()

enable_testing()
add_subdirectory(Source)
