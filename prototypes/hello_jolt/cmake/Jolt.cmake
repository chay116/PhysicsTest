# cmake/ThirdParty/Jolt.cmake -- include() from the root CMakeLists AFTER CMAKE_MSVC_RUNTIME_LIBRARY is set.
include_guard(GLOBAL)
include(FetchContent)
set(PT_JOLT_GIT_REPOSITORY "https://github.com/jrouwe/JoltPhysics.git" CACHE STRING "Jolt git repository (URL or local clone path)")
set(PT_JOLT_GIT_TAG        "v5.6.0"                                   CACHE STRING "Jolt tag (v5.6.0 = e77f175595e64cb44218cc9d9d56fc365ad0e36a)")
option(PT_JOLT_AVX2 "Build Jolt with AVX2 (+FMA/F16C/LZCNT/TZCNT); OFF = SSE2 baseline" ON)
# Use a local checkout without network:  -DFETCHCONTENT_SOURCE_DIR_JOLTPHYSICS=<path to JoltPhysics root>

FetchContent_Declare(JoltPhysics
	GIT_REPOSITORY ${PT_JOLT_GIT_REPOSITORY}
	GIT_TAG        ${PT_JOLT_GIT_TAG}
	GIT_SHALLOW    TRUE
	SOURCE_SUBDIR  Build)                 # Jolt's CMake entry point is Build/CMakeLists.txt

block(SCOPE_FOR VARIABLES)                # keep Jolt's generic option names (USE_AVX2, ENABLE_INSTALL, ...) out of Bullet/PhysX scopes
	# Plain variables; Jolt's option() honours them (its cmake_minimum_required(3.20) => CMP0077 NEW).
	set(USE_STATIC_MSVC_RUNTIME_LIBRARY     OFF) # default ON = /MT -> LNK2038 against /MD code
	set(INTERPROCEDURAL_OPTIMIZATION        OFF) # default ON = /GL on Jolt only -> same LTO policy for every engine
	set(ENABLE_ALL_WARNINGS                 OFF) # default ON = /Wall /WX (Jolt dir only); avoid breakage on MSVC updates
	set(ENABLE_INSTALL                      OFF) # default ON
	set(JPH_USE_DX12                        OFF) # default ON: hair compute; writes *.dxil into the SOURCE tree
	set(JPH_USE_VK                          OFF) # default ON: writes *.spv into the SOURCE tree when Vulkan SDK found
	set(JPH_USE_CPU_COMPUTE                 OFF) # default ON: hair CPU fallback only
	set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF) # default ON: we draw from our own scene data, not Jolt's DebugRenderer
	set(PROFILER_IN_DEBUG_AND_RELEASE       OFF) # default ON: JPH_PROFILE_ENABLED costs ~1.3-2x in Release
	set(FLOATING_POINT_EXCEPTIONS_ENABLED   OFF) # default ON: FP traps on Jolt worker threads + Vec3 W-lane fixups
	# Left at defaults: DOUBLE_PRECISION OFF, CROSS_PLATFORM_DETERMINISTIC OFF (/fp:fast), USE_ASSERTS OFF,
	# OBJECT_LAYER_BITS (16), ENABLE_OBJECT_STREAM ON, CPP_RTTI_ENABLED OFF (/GR-), CPP_EXCEPTIONS_ENABLED OFF,
	# OVERRIDE_CXX_FLAGS ON (Release = /GS- /Gy /O2 /Oi /Ot), GENERATE_DEBUG_SYMBOLS ON (/Zi).
	if (NOT PT_JOLT_AVX2)
		foreach(_isa USE_SSE4_1 USE_SSE4_2 USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
			set(${_isa} OFF)
		endforeach()
	endif()
	FetchContent_MakeAvailable(JoltPhysics)
endblock()

# Jolt.cmake adds /arch:AVX2 (+ JPH_USE_* defines) only when CMAKE_VS_PLATFORM_NAME is x86/x64, i.e. only for
# Visual Studio generators. Under Ninja + cl.exe it silently builds SSE2-only. Jolt/Core/Core.h derives every
# JPH_USE_* from __AVX2__, so a PUBLIC /arch:AVX2 keeps the library and its consumers consistent.
if (MSVC AND PT_JOLT_AVX2 AND NOT CMAKE_GENERATOR MATCHES "Visual Studio")
	target_compile_options(Jolt PUBLIC /arch:AVX2)
endif()
