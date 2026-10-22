# PT::Core - minimal UE idioms (02_Architecture 3, A.5, DL18): math, UE_LOG, check/verify/ensure, FCommandLine/FParse,
# FPlatformTime (QPC), FNV-1a / SHA-256, FRandomStream, UTF-8 string conversion, the harness-side third-party include
# pair (M0), FConfigFile and IConsoleManager (M1), FColor / FMatrix / FSphere for the debug draw (M4), FCycleCounter (TSC, rdtscp + lfence, QPC-calibrated; M10).
pt_add_module(PtCore STATIC ALIAS PT::Core
	SOURCES
		Public/CoreMinimal.h
		Public/CoreTypes.h
		Public/Containers/StringConv.h
		Public/HAL/IConsoleManager.h
		Public/HAL/PlatformMisc.h
		Public/HAL/PlatformTime.h
		Public/Hash/Fnv.h
		Public/Hash/Sha256.h
		Public/Logging/LogMacros.h
		Public/Math/Box.h
		Public/Math/Color.h
		Public/Math/Matrix.h
		Public/Math/Quat.h
		Public/Math/RandomStream.h
		Public/Math/Rotator.h
		Public/Math/Sphere.h
		Public/Math/Transform.h
		Public/Math/UnrealMathUtility.h
		Public/Math/Vector.h
		Public/Misc/AssertionMacros.h
		Public/Misc/CommandLine.h
		Public/Misc/ConfigCacheIni.h
		Public/Misc/Parse.h
		Public/Misc/PtThirdPartyIncludesStart.h
		Public/Misc/PtThirdPartyIncludesEnd.h
		Public/ProfilingDebugging/PtProfiler.h
		Private/Containers/StringConv.cpp
		Private/HAL/ConsoleManager.cpp
		Private/HAL/CycleCounter.cpp
		Private/HAL/WindowsPlatform.cpp
		Private/Hash/Sha256.cpp
		Private/Logging/LogMacros.cpp
		Private/Math/Math.cpp
		Private/Math/Matrix.cpp
		Private/Math/RandomStream.cpp
		Private/Misc/AssertionMacros.cpp
		Private/Misc/CommandLine.cpp
		Private/Misc/ConfigCacheIni.cpp
		Private/Misc/Parse.cpp)

# Usage requirements of Core's PUBLIC headers, so every consumer compiles them the same way even without
# pt_target_defaults() (which also sets them, PRIVATE, on every harness target; CMake de-duplicates the flags):
#   C++20                PUBLIC compile feature, set by pt_target_defaults() above
#   /Zc:preprocessor     __VA_OPT__ in check / checkf / UE_LOG (Misc/AssertionMacros.h, Logging/LogMacros.h)
#   PT_DO_CHECK=0|1      build-global like UE's DO_CHECK: the inline check/ensure macros must expand identically in
#                        every TU (ODR). CoreTypes.h stops with #error when it is missing.
target_compile_options(PtCore PUBLIC /Zc:preprocessor)
target_compile_definitions(PtCore PUBLIC PT_DO_CHECK=$<IF:$<BOOL:${PT_DO_CHECK}>,1,0>)
#   PT_WITH_PROFILER=0|1 (M10) the prof preset's Tracy zones (ProfilingDebugging/PtProfiler.h); with 1 every harness program links
#                        the Tracy client (PT::Tracy, cmake/ThirdParty/Tracy.cmake)
target_compile_definitions(PtCore PUBLIC PT_WITH_PROFILER=$<IF:$<BOOL:${PT_WITH_PROFILER}>,1,0>)
if(PT_WITH_PROFILER)
	target_link_libraries(PtCore PUBLIC PT::Tracy)
endif()
