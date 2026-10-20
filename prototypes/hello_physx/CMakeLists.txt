cmake_minimum_required(VERSION 3.21)
project(hello_physx LANGUAGES C CXX)

# How to consume PhysX:
#   package : find_package(PhysX CONFIG) from an installed prefix (CMAKE_PREFIX_PATH / PhysX_DIR)
#   manual  : raw PHYSX_ROOT (PhysX's classic bin/win.x86_64.vc143.md/<cfg>/ layout), UE4-style explicit lib list
#   subdir  : add_subdirectory(<physx src>) -> target physx_lib (same as FetchContent SOURCE_SUBDIR physx)
set(PHYSX_CONSUME "package" CACHE STRING "package|manual|subdir")
set_property(CACHE PHYSX_CONSUME PROPERTY STRINGS package manual subdir)

add_executable(hello_physx main.cpp)
target_compile_features(hello_physx PRIVATE cxx_std_17)
set_property(TARGET hello_physx PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if(PHYSX_CONSUME STREQUAL "package")
  find_package(PhysX 5.11 CONFIG REQUIRED)
  message(STATUS "PhysX package: ${PhysX_DIR} (version ${PhysX_VERSION})")
  # Only the libs we need; link order is resolved by the exported INTERFACE_LINK_LIBRARIES.
  target_link_libraries(hello_physx PRIVATE PhysX::PhysXExtensions PhysX::PhysX PhysX::PhysXPvdSDK PhysX::PhysXCommon PhysX::PhysXFoundation)
  # The exported targets only carry DISABLE_CUDA_PHYSX; PX_PHYSX_STATIC_LIB lives in the generated PxConfig.h which
  # no PhysX header includes. Without this define every PhysX API is declared __declspec(dllimport) -> LNK2019.
  target_compile_definitions(hello_physx PRIVATE PX_PHYSX_STATIC_LIB)

elseif(PHYSX_CONSUME STREQUAL "manual")
  set(PHYSX_ROOT "" CACHE PATH "PhysX source root (contains include/) ")
  set(PHYSX_BIN_ROOT "" CACHE PATH "Dir containing bin/win.x86_64.vc143.md/<config>/")
  if(NOT EXISTS "${PHYSX_ROOT}/include/PxPhysicsAPI.h")
    message(FATAL_ERROR "PHYSX_ROOT invalid: ${PHYSX_ROOT}")
  endif()
  set(_pxlib "${PHYSX_BIN_ROOT}/bin/win.x86_64.vc143.md")
  # Map our configs onto PhysX's 4 configs: Debug->debug, RelWithDebInfo->profile (PVD on), Release/MinSizeRel->release
  set(_pxcfg "$<IF:$<CONFIG:Debug>,debug,$<IF:$<CONFIG:RelWithDebInfo>,profile,release>>")
  add_library(PhysXSDK INTERFACE)
  target_include_directories(PhysXSDK INTERFACE "${PHYSX_ROOT}/include")
  target_compile_definitions(PhysXSDK INTERFACE DISABLE_CUDA_PHYSX PX_PHYSX_STATIC_LIB)
  # Link order (static, dependents first): Extensions -> PhysX -> PvdSDK -> Common -> Foundation
  set(PHYSX_LIB_LIST "PhysXExtensions;PhysX;PhysXPvdSDK;PhysXCharacterKinematic;PhysXCooking;PhysXCommon;PhysXFoundation" CACHE STRING "")
  foreach(_l IN LISTS PHYSX_LIB_LIST)
    target_link_libraries(PhysXSDK INTERFACE "${_pxlib}/${_pxcfg}/${_l}_static_64.lib")
  endforeach()
  set(PHYSX_EXTRA_SYSLIBS "" CACHE STRING "e.g. ws2_32 when PVDRuntime (OmniPVD) is linked")
  target_link_libraries(PhysXSDK INTERFACE ${PHYSX_EXTRA_SYSLIBS})
  target_link_libraries(hello_physx PRIVATE PhysXSDK)

elseif(PHYSX_CONSUME STREQUAL "subdir")
  set(PHYSX_SOURCE_DIR "" CACHE PATH "PhysX 'physx' dir (contains CMakeLists.txt)")
  set(PX_GENERATE_GPU_PROJECTS OFF CACHE BOOL "" FORCE)
  set(PX_GENERATE_STATIC_LIBRARIES ON CACHE BOOL "" FORCE)
  set(NV_USE_STATIC_WINCRT OFF CACHE BOOL "" FORCE)
  set(PX_BUILDPVDRUNTIME OFF CACHE BOOL "" FORCE)
  set(PX_BUILDSNIPPETS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${PHYSX_SOURCE_DIR}" px)
  target_link_libraries(hello_physx PRIVATE physx_lib)
  target_compile_definitions(hello_physx PRIVATE PX_PHYSX_STATIC_LIB)
  message(STATUS "After PhysX subdir: CMAKE_CONFIGURATION_TYPES='${CMAKE_CONFIGURATION_TYPES}' CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}' CMAKE_CXX_FLAGS_RELEASE='${CMAKE_CXX_FLAGS_RELEASE}' CMAKE_MSVC_RUNTIME_LIBRARY='${CMAKE_MSVC_RUNTIME_LIBRARY}'")
else()
  message(FATAL_ERROR "Unknown PHYSX_CONSUME=${PHYSX_CONSUME}")
endif()
