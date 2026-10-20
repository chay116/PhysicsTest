cmake_minimum_required(VERSION 3.25)
project(PhysicsTestProto LANGUAGES C CXX)

# C++20 only on OUR targets: a global CMAKE_CXX_STANDARD leaks into PhysX (/std:c++14 /WX) and breaks it (C5055)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# One CRT for everything (/MD, /MDd)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)

option(PROTO_WITH_JOLT   "Jolt"   ON)
option(PROTO_WITH_BULLET "Bullet" ON)
option(PROTO_WITH_PHYSX  "PhysX"  ON)
set(PROTO_JOLT_GIT   "https://github.com/jrouwe/JoltPhysics.git"     CACHE STRING "")
set(PROTO_BULLET_GIT "https://github.com/bulletphysics/bullet3.git"   CACHE STRING "")
set(PROTO_PHYSX_GIT  "https://github.com/NVIDIA-Omniverse/PhysX.git"  CACHE STRING "")

set(PROTO_ARCH "" CACHE STRING "Uniform x64 ISA for ALL targets incl. SDKs: '' (SSE2) | AVX | AVX2")
if(PROTO_ARCH)
  add_compile_options(/arch:${PROTO_ARCH})   # directory property -> inherited by SDK subdirs, survives their CMAKE_CXX_FLAGS overrides
endif()
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------- small deps: URL + SHA256 ----------------
FetchContent_Declare(imgui  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b-docking.tar.gz
                            URL_HASH SHA256=90ded916bd57db2e0e171b6b098940a47c6f5042725dcdc67fb19940ca8bfdcc)
FetchContent_Declare(implot URL https://github.com/epezent/implot/archive/refs/tags/v1.0.tar.gz
                            URL_HASH SHA256=e4a9db64eef7bcc604e2a5ea380af124eb97aa3e8a6a96330079c88add0f7e93)
FetchContent_Declare(stb    URL https://github.com/nothings/stb/archive/a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d.tar.gz
                            URL_HASH SHA256=ae498e10fadeba0c2e7aa86c71cc9989c4d895201cd26b8b72061c76cfde4edf)
FetchContent_MakeAvailable(imgui implot stb)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${implot_SOURCE_DIR}/implot.cpp ${implot_SOURCE_DIR}/implot_items.cpp ${implot_SOURCE_DIR}/implot_demo.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${implot_SOURCE_DIR})
# NOTE: IMGUI_DISABLE_OBSOLETE_FUNCTIONS breaks implot v1.0 with imgui >= 1.92.8 (AddPolyline arg swap)
target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler)

add_executable(proto_render src/proto_render.cpp)
target_include_directories(proto_render PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(proto_render PRIVATE imgui)
target_compile_options(proto_render PRIVATE /W4 /permissive- /utf-8)
target_compile_features(proto_render PRIVATE cxx_std_20)

# ---------------- physics SDKs ----------------
if(PROTO_WITH_JOLT)
  set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
  foreach(o JPH_USE_DX12 JPH_USE_VK JPH_USE_MTL JPH_USE_CPU_COMPUTE ENABLE_INSTALL INTERPROCEDURAL_OPTIMIZATION
            DEBUG_RENDERER_IN_DEBUG_AND_RELEASE PROFILER_IN_DEBUG_AND_RELEASE ENABLE_ALL_WARNINGS)
    set(${o} OFF CACHE BOOL "" FORCE)
  endforeach()
  FetchContent_Declare(JoltPhysics GIT_REPOSITORY ${PROTO_JOLT_GIT} GIT_TAG v5.6.0 GIT_SHALLOW TRUE SOURCE_SUBDIR Build)
  FetchContent_MakeAvailable(JoltPhysics)
  add_executable(hello_jolt src/hello_jolt.cpp)
  target_link_libraries(hello_jolt PRIVATE Jolt)
  target_compile_features(hello_jolt PRIVATE cxx_std_20)  # Jolt target does not propagate a C++ standard
endif()

if(PROTO_WITH_BULLET)
  # SOURCE_SUBDIR without CMakeLists.txt => populate only, Bullet's own (cmake 2.4.3-era) CMakeLists is never run
  FetchContent_Declare(bullet3 GIT_REPOSITORY ${PROTO_BULLET_GIT} GIT_TAG 3.25 GIT_SHALLOW TRUE GIT_CONFIG core.longpaths=true SOURCE_SUBDIR __populate_only__)
  FetchContent_MakeAvailable(bullet3)
  # NOTE: src/btLinearMathAll.cpp is broken on MSVC (btVector3.cpp #defines BT_USE_SSE_IN_API mid-TU) -> compile per file
  file(GLOB_RECURSE BULLET_SRC CONFIGURE_DEPENDS
    ${bullet3_SOURCE_DIR}/src/LinearMath/*.cpp ${bullet3_SOURCE_DIR}/src/BulletCollision/*.cpp ${bullet3_SOURCE_DIR}/src/BulletDynamics/*.cpp)
  add_library(BulletUnity STATIC ${BULLET_SRC})
  target_include_directories(BulletUnity SYSTEM PUBLIC ${bullet3_SOURCE_DIR}/src)
  target_compile_options(BulletUnity PRIVATE /W0 /fp:fast)
  add_executable(hello_bullet src/hello_bullet.cpp)
  target_link_libraries(hello_bullet PRIVATE BulletUnity)
endif()

if(PROTO_WITH_PHYSX)
  set(PX_GENERATE_GPU_PROJECTS OFF CACHE BOOL "" FORCE)
  set(PX_GENERATE_STATIC_LIBRARIES ON CACHE BOOL "" FORCE)
  set(NV_USE_STATIC_WINCRT OFF CACHE BOOL "" FORCE)
  set(PX_BUILDSNIPPETS OFF CACHE BOOL "" FORCE)
  set(PX_BUILDPVDRUNTIME OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(physx GIT_REPOSITORY ${PROTO_PHYSX_GIT} GIT_TAG ovphysx-0.6.3 GIT_SHALLOW TRUE SOURCE_SUBDIR physx)
  FetchContent_MakeAvailable(physx)
  if(CMAKE_CONFIGURATION_TYPES AND PROTO_RESTORE_CONFIGS)  # PhysX FORCEs debug;checked;profile;release on multi-config generators
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
  endif()
  add_executable(hello_physx src/hello_physx.cpp)
  target_link_libraries(hello_physx PRIVATE physx_lib)
  target_compile_definitions(hello_physx PRIVATE PX_PHYSX_STATIC_LIB)  # physx_lib does NOT export it; PxConfig.h is never included
  target_compile_features(hello_physx PRIVATE cxx_std_20)
endif()


