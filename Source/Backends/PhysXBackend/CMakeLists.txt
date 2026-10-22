# PT::Backend::PhysX - PhysX 5.11 behind the C ABI, UE4-style recipe re-implemented clean room (02_Architecture 4.5, 8.2,
# A.14; 03_WorkPlan M5). Links PT::BackendCommon (-> PT::Abi) and PT::PhysX PRIVATE: PhysX's include path and PX_*
# definitions stay in this library's TUs (CTest PhysicsTest.Probe.HarnessSdkIsolation). No harness library is linked (R4).
# PT_BACKEND_NS=PhysX keeps the Kit / BackendCommon symbols in their own inline namespace (DL46, Probe.KitNamespaces).
# The adapter TUs keep CMake's /GR /EHsc, so the Kit's exception barrier works as in every harness build; the adapter's
# containers are physx::PxArray (PhysX's allocator), never a std:: container (docs/Abi.md 5.13).
set(_pt_physx_sources
	Private/PhysicsFiltering.cpp
	Private/PhysicsFiltering.h
	Private/PhysicsInitialization.cpp
	Private/PhysicsInitialization.h
	Private/PhysScene_PhysX.cpp
	Private/PhysScene_PhysX.h
	Private/PhysXBackend.cpp
	Private/PhysXConstraint.cpp
	Private/PhysXConstraint.h
	Private/PhysXCPUDispatcher.cpp
	Private/PhysXCPUDispatcher.h
	Private/PhysXDebugRender.cpp
	Private/PhysXDebugRender.h
	Private/PhysXIncludes.h
	Private/PhysXParse.h
	Private/PhysXSceneConfig.cpp
	Private/PhysXSceneConfig.h
	Private/PhysXSimEventCallback.cpp
	Private/PhysXSimEventCallback.h
	Private/PhysXSupport.cpp
	Private/PhysXSupport.h)

add_library(PtBackendPhysX STATIC ${_pt_physx_sources} Public/PhysXBackend/PhysXBackendEntry.h Public/PhysXBackend/PhysXBackendTestHooks.h)
add_library(PT::Backend::PhysX ALIAS PtBackendPhysX)
target_include_directories(PtBackendPhysX PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_link_libraries(PtBackendPhysX PRIVATE PT::BackendCommon PT::PhysX INTERFACE PT::Abi)
target_compile_definitions(PtBackendPhysX PRIVATE PT_BACKEND_NS=PhysX "PT_PHYSX_SOURCE_COMMIT=\"${PT_PHYSX_PIN_SHA}\"")
# ODR / flag isolation (docs/Abi.md 5.13), as for Bullet: in the shipped presets this adapter has the harness's /arch, but a
# matched-AVX2 or per-engine flag preset (test-avx2, test-physx: M7 / M10) compiles it differently. The UCRT's inline stdio
# functions (snprintf, vsnprintf, __local_stdio_printf_options) are kept out of these objects, so they resolve to a harness
# copy or to legacy_stdio_definitions.lib, never to an adapter copy. The std::atomic members of FPhysXAllocator /
# FPhysXErrorCallback (PhysXSupport.h) are template COMDATs outside the backend namespace: Probe.AdapterComdatsPhysX must
# be re-checked under any preset where this adapter's /arch differs from the harness's.
target_compile_definitions(PtBackendPhysX PRIVATE _NO_CRT_STDIO_INLINE)
target_link_libraries(PtBackendPhysX INTERFACE legacy_stdio_definitions)
# Generated backend registry (Source/Backends/CMakeLists.txt): the shared SDK suites, the bench and the viewer take this
# backend, and PhysicsTest.PhysXBackend.Registered requires the registry to list it.
set_target_properties(PtBackendPhysX PROPERTIES PT_IN_BACKEND_REGISTRY ON)
pt_target_defaults(PtBackendPhysX)
set_property(TARGET PtBackendPhysX PROPERTY FOLDER "PhysicsTest/Backends")
