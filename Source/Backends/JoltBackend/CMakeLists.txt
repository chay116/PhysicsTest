# PT::Backend::Jolt - Jolt Physics 5.6.0 behind the C ABI (02_Architecture 4.5, 8.3, A.15; 03_WorkPlan M3).
# Links PT::BackendCommon (-> PT::Abi) and PT::Jolt PRIVATE: Jolt's PUBLIC /arch:AVX2 and JPH_* definitions stay in this
# library's TUs (02 4.2, CTest PhysicsTest.Probe.FlagLeak / HarnessSdkIsolation). No harness library is linked (R4).
# PT_BACKEND_NS=Jolt keeps the Kit / BackendCommon symbols in their own inline namespace (DL46, Probe.KitNamespaces).
# The adapter TUs keep CMake's /GR /EHsc (Jolt itself is /GR- without exceptions), so the Kit's exception barrier works
# as in every harness build; the adapter's containers are JPH::Array, which does not throw (docs/Progress.md M3 §3 #13).
set(_pt_jolt_sources
	Private/JoltBackend.cpp
	Private/JoltCombine.h
	Private/JoltContactListener.h
	Private/JoltIncludes.h
	Private/JoltLayers.h
	Private/JoltTempAllocator.h
	Private/PhysScene_Jolt.cpp
	Private/PhysScene_Jolt.h)

add_library(PtBackendJolt STATIC ${_pt_jolt_sources} Public/JoltBackend/JoltBackendEntry.h)
add_library(PT::Backend::Jolt ALIAS PtBackendJolt)
target_include_directories(PtBackendJolt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_link_libraries(PtBackendJolt PRIVATE PT::BackendCommon PT::Jolt INTERFACE PT::Abi)
target_compile_definitions(PtBackendJolt PRIVATE PT_BACKEND_NS=Jolt "PT_JOLT_SOURCE_COMMIT=\"${PT_JOLT_PIN_SHA}\"")
# ODR / flag isolation (docs/Abi.md 5.13): these TUs are /arch:AVX2, so they must not define a COMDAT the harness also
# defines. The adapter's containers are JPH::Array (PhysScene_Jolt.h), and _NO_CRT_STDIO_INLINE keeps the UCRT's inline
# stdio functions (snprintf, vsnprintf, __local_stdio_printf_options) out of these objects: they resolve to a harness
# copy or to legacy_stdio_definitions.lib, never to an AVX2 copy. CTest PhysicsTest.Probe.AdapterComdats checks the maps.
target_compile_definitions(PtBackendJolt PRIVATE _NO_CRT_STDIO_INLINE)
target_link_libraries(PtBackendJolt INTERFACE legacy_stdio_definitions)
pt_target_defaults(PtBackendJolt)
set_property(TARGET PtBackendJolt PROPERTY FOLDER "PhysicsTest/Backends")
