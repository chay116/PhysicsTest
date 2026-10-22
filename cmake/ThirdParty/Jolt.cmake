# Jolt.cmake - Jolt Physics v5.6.0 (e77f1755) -> PT::Jolt (02_Architecture 4.3, D3).
# Include AFTER PTToolchain.cmake (CMAKE_MSVC_RUNTIME_LIBRARY must already be /MD).
#
# Backends link PT::Jolt PRIVATE: Jolt's /arch:AVX2 and JPH_* defines are PUBLIC on the Jolt target, so a PRIVATE
# link keeps them inside the backend library (checked by the PhysicsTest.Probe.FlagLeak CTest).
include_guard(GLOBAL)
include(PtFetch)

pt_fetch_git_pinned(jolt URL "${PT_JOLT_GIT}" TAG "${PT_JOLT_GIT_TAG}" PIN "${PT_JOLT_PIN_SHA}")

block(SCOPE_FOR VARIABLES)   # keep Jolt's generic option names (USE_AVX2, ENABLE_INSTALL, ...) out of other scopes
	# Plain variables: Jolt's option() honours them (its cmake_minimum_required(3.20) => CMP0077 NEW).
	set(USE_STATIC_MSVC_RUNTIME_LIBRARY     OFF) # default ON = /MT -> LNK2038 against our /MD code
	set(INTERPROCEDURAL_OPTIMIZATION        OFF) # default ON = /GL on Jolt only; no engine gets LTO
	set(ENABLE_ALL_WARNINGS                 OFF) # default ON = /Wall /WX in Jolt's directory
	set(ENABLE_INSTALL                      OFF)
	set(JPH_USE_DX12                        OFF) # default ON: hair compute; writes *.dxil into the SOURCE tree
	set(JPH_USE_VK                          OFF) # default ON: writes *.spv into the SOURCE tree when Vulkan is found
	set(JPH_USE_MTL                         OFF)
	set(JPH_USE_CPU_COMPUTE                 OFF)
	set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF) # we draw from our own scene data (M4), not Jolt's DebugRenderer
	set(PROFILER_IN_DEBUG_AND_RELEASE       OFF) # JPH_PROFILE_ENABLED costs ~1.3-2x; prof preset only (M10)
	set(JPH_USE_EXTERNAL_PROFILE            OFF)
	if(PT_WITH_PROFILER)
		# M10 prof preset: Jolt's JPH_PROFILE scopes become Tracy zones (JPH_EXTERNAL_PROFILE: the adapter implements
		# JPH::ExternalProfileMeasurement in JoltProfiler.cpp). Jolt's configuration string then says "(External Profile)",
		# which G0 accepts only in a profiler build (BenchPreflight.cpp).
		set(PROFILER_IN_DEBUG_AND_RELEASE   ON)
		set(JPH_USE_EXTERNAL_PROFILE        ON)
	endif()
	set(FLOATING_POINT_EXCEPTIONS_ENABLED   OFF) # FP traps on Jolt threads + Vec3 W-lane fixups
	# Left at defaults: DOUBLE_PRECISION OFF, CROSS_PLATFORM_DETERMINISTIC OFF (/fp:fast), USE_ASSERTS OFF,
	# OBJECT_LAYER_BITS 16, ENABLE_OBJECT_STREAM ON, CPP_RTTI_ENABLED OFF (/GR-), CPP_EXCEPTIONS_ENABLED OFF,
	# OVERRIDE_CXX_FLAGS ON (Release = /GS- /Gy /O2 /Oi /Ot), GENERATE_DEBUG_SYMBOLS ON (/Zi).
	add_subdirectory("${PT_JOLT_SOURCE_DIR}/Build" "${PT_JOLT_BINARY_DIR}" EXCLUDE_FROM_ALL SYSTEM)
endblock()
set_property(TARGET Jolt PROPERTY FOLDER "ThirdParty/Jolt")

# Jolt/Jolt.cmake:969-979 adds /arch:AVX2 only when CMAKE_VS_PLATFORM_NAME is x64, i.e. for Visual Studio
# generators. Under Ninja it silently builds SSE2-only (about 1.8x slower, 01_Feasibility 3.3). Jolt/Core/Core.h
# derives every JPH_USE_* from __AVX2__, so a PUBLIC /arch keeps the library and its consumers consistent.
if(PT_ARCH_JOLT AND NOT CMAKE_GENERATOR MATCHES "Visual Studio")
	pt_apply_arch(ARCH ${PT_ARCH_JOLT} SCOPE PUBLIC TARGETS Jolt)
endif()

add_library(pt_jolt INTERFACE)
add_library(PT::Jolt ALIAS pt_jolt)
target_link_libraries(pt_jolt INTERFACE Jolt)
