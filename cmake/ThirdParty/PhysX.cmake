# PhysX.cmake - PhysX SDK 5.11.0 (da950a35, tag ovphysx-0.6.3), CPU-only static -> PT::PhysX (02 4.3, DL54).
# Main path for single-config Ninja builds. PhysXBootstrap.cmake is the alternative (VS generators, or when the
# PhysX on/off flag comparison of 03 M0 criterion 4 fails, risk R6).
#
# Why this path is safe for the parent build [V]:
#  * The block that FORCE-rewrites the parent's configuration list and CMAKE_CXX_FLAGS_<CONFIG> runs only
#    `if(CMAKE_CONFIGURATION_TYPES)` (physx/buildtools/cmake/PhysXConfigurationTypes.cmake:12-36): never under Ninja.
#  * As a subproject PhysX keeps the parent's CMAKE_MSVC_RUNTIME_LIBRARY (physx/CMakeLists.txt:131-134).
#  * PhysX overrides CMAKE_CXX_FLAGS only in its own directory (physx/source/compiler/cmake/windows/CMakeLists.txt:65-70).
#  * PxConfig.h is written into the clone under out/<preset>/_deps/physx-src, never into the mirror.
#  * With a single-config generator the PhysX configuration follows $<CONFIG>: Debug -> debug, Release -> release
#    (physx/source/compiler/cmake/windows/PhysX.cmake:94-101). BuildFlags.json records the configuration seen in
#    PhysX's own TUs.
include_guard(GLOBAL)
include(PtFetch)

pt_fetch_git_pinned(physx URL "${PT_PHYSX_GIT}" TAG "${PT_PHYSX_GIT_TAG}" PIN "${PT_PHYSX_PIN_SHA}")

macro(_pt_physx_force Name Value)
	set(${Name} ${Value} CACHE BOOL "Forced by PhysX.cmake" FORCE)
endmacro()
_pt_physx_force(PX_GENERATE_GPU_PROJECTS OFF)       # CPU only, no CUDA
_pt_physx_force(PX_GENERATE_STATIC_LIBRARIES ON)
_pt_physx_force(NV_USE_STATIC_WINCRT OFF)           # /MD like everything else
_pt_physx_force(NV_USE_DEBUG_WINCRT ON)             # Debug -> /D_DEBUG
_pt_physx_force(PX_BUILDSNIPPETS OFF)
_pt_physx_force(PX_BUILDPVDRUNTIME OFF)
_pt_physx_force(PX_FLOAT_POINT_PRECISE_MATH OFF)    # PhysX's own /fp:fast (D5)

# Known risk (M0 review): every PhysX TU compiles with PhysX's own /WX (physx/source/compiler/cmake/windows/
# CMakeLists.txt:23). A newer MSVC that adds a warning in SDK code would break the build. 0 warnings with MSVC 14.44
# (cl 19.44.35229). If a toolchain bump breaks it, append /WX- to the PhysX targets here (after add_subdirectory)
# instead of silencing individual warnings, and record it in docs/Progress.md.
add_subdirectory("${PT_PHYSX_SOURCE_DIR}/physx" "${PT_PHYSX_BINARY_DIR}" EXCLUDE_FROM_ALL SYSTEM)
if(CMAKE_CONFIGURATION_TYPES AND NOT PT_MULTI_CONFIG)
	message(FATAL_ERROR "PhysX changed CMAKE_CONFIGURATION_TYPES under a single-config generator")
endif()

# physx_lib (physx/CMakeLists.txt:326) aggregates the SDK libraries and the public include directories but does not
# carry PX_PHYSX_STATIC_LIB (it lives only in the generated PxConfig.h, which no PhysX header includes). Without it
# every API is declared __declspec(dllimport) -> LNK2019 __imp_PxCreatePhysics.
# PhysXPvdSDK is a link dependency of PhysX and PhysXExtensions in every configuration
# (physx/source/compiler/cmake/PhysX.cmake:397-400, PhysXExtensions.cmake:319-324), so it cannot be Debug-only;
# in release its code is compiled out (PX_SUPPORT_PVD=0).
# Decided in M5 (the PhysX adapter, Source/Backends/PhysXBackend/Private/PhysXIncludes.h): the PhysX libraries are built
# with per-configuration macros (Debug: PX_DEBUG=1 PX_CHECKED=1 PX_SUPPORT_PVD=1; Release: PX_SUPPORT_PVD=0;
# physx/source/compiler/cmake/windows/CMakeLists.txt:109-112), consumers get only the two below, so an adapter TU sees
# PX_DEBUG = PX_CHECKED = 0 (PxPreprocessor.h defaults). They are deliberately NOT forwarded: no public PhysX header
# changes a type layout on them - they only gate inline parameter checks, PX_WARN_ONCE, PX_ASSERT and profile zones
# (PxErrors.h:60-90, PxAssert.h, PxProfileZone.h, PxPhysXCommonConfig.h:11) - so the adapter sees release-style inline
# helpers in every configuration. In dbg the adapter's and the checked libraries' copies of shared inline / template
# functions (PxArray, PxVec3, ... with PX_ASSERT inside) are ODR-different COMDATs and the linker keeps one of them
# (M6, M5 review: benign, dbg is not a measurement build; see PhysXIncludes.h). The dev/test choice of PhysX "release"
# (UE4 Development may have linked PROFILE) is a UE4Compat question for the user (04_DecisionLog Q19).
add_library(pt_physx INTERFACE)
add_library(PT::PhysX ALIAS pt_physx)
target_link_libraries(pt_physx INTERFACE physx_lib)
target_compile_definitions(pt_physx INTERFACE PX_PHYSX_STATIC_LIB DISABLE_CUDA_PHYSX)

get_target_property(_pt_physx_libs physx_lib INTERFACE_LINK_LIBRARIES)
foreach(_t IN LISTS _pt_physx_libs)
	if(TARGET ${_t})
		set_property(TARGET ${_t} PROPERTY FOLDER "ThirdParty/PhysX")
	endif()
endforeach()

# MatchedAVX2 (M7, test-avx2): /arch on every PhysX target, PUBLIC like Jolt's, so the PhysX adapter (PT::Backend::PhysX,
# linked PRIVATE) and the smoke program compile PhysX's inline code with the same ISA as the libraries; nothing above the
# adapter sees it (PTFlagProfiles.cmake). Shipped: PT_ARCH_PHYSX is empty and PhysX keeps its vendor default (SSE2).
if(PT_ARCH_PHYSX)
	pt_apply_arch(ARCH ${PT_ARCH_PHYSX} SCOPE PUBLIC DIRECTORIES "${PT_PHYSX_SOURCE_DIR}/physx")
endif()
