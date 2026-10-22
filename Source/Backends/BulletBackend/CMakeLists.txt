# PT::Backend::Bullet - Bullet 3.25 behind the C ABI (02_Architecture 4.5, 8.4, A.15; 03_WorkPlan M5).
# Links PT::BackendCommon (-> PT::Abi) and PT::Bullet PRIVATE: Bullet's include path and BT_* definitions stay in this
# library's TUs (CTest PhysicsTest.Probe.HarnessSdkIsolation). No harness library is linked (R4).
# PT_BACKEND_NS=Bullet keeps the Kit / BackendCommon symbols in their own inline namespace (DL46, Probe.KitNamespaces).
# The adapter TUs keep CMake's /GR /EHsc, so the Kit's exception barrier works as in every harness build; the adapter's
# containers are btAlignedObjectArray (Bullet's own type, docs/Abi.md 5.13).
set(_pt_bullet_sources
	Private/BulletBackend.cpp
	Private/BulletCombine.h
	Private/BulletDebugDrawer.cpp
	Private/BulletDebugDrawer.h
	Private/BulletIncludes.h
	Private/BulletMath.h
	Private/PhysScene_Bullet.cpp
	Private/PhysScene_Bullet.h
	Private/PhysScene_BulletJoints.cpp)

add_library(PtBackendBullet STATIC ${_pt_bullet_sources} Public/BulletBackend/BulletBackendEntry.h)
add_library(PT::Backend::Bullet ALIAS PtBackendBullet)
target_include_directories(PtBackendBullet PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_link_libraries(PtBackendBullet PRIVATE PT::BackendCommon PT::Bullet INTERFACE PT::Abi)
target_compile_definitions(PtBackendBullet PRIVATE PT_BACKEND_NS=Bullet "PT_BULLET_SOURCE_COMMIT=\"${PT_BULLET_PIN_SHA}\"")
# ODR / flag isolation (docs/Abi.md 5.13): in the shipped presets this adapter has the harness's /arch, but a matched-AVX2
# or per-engine flag preset (test-avx2, test-bullet: M7 / M10) compiles it differently. As for Jolt, the UCRT's inline
# stdio functions (snprintf, vsnprintf, __local_stdio_printf_options) are kept out of these objects, so they resolve to a
# harness copy or to legacy_stdio_definitions.lib, never to an adapter copy.
target_compile_definitions(PtBackendBullet PRIVATE _NO_CRT_STDIO_INLINE)
target_link_libraries(PtBackendBullet INTERFACE legacy_stdio_definitions)
# Generated backend registry (Source/Backends/CMakeLists.txt): ON since M5 (CreateScene works). The shared SDK suites, the
# bench and the viewer take this backend, and PhysicsTest.BulletBackend.Registered requires the registry to list it.
set_target_properties(PtBackendBullet PROPERTIES PT_IN_BACKEND_REGISTRY ON)
pt_target_defaults(PtBackendBullet)
set_property(TARGET PtBackendBullet PROPERTY FOLDER "PhysicsTest/Backends")
