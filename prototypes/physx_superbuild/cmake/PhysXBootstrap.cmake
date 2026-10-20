# PhysXBootstrap.cmake -- build PhysX 5.x (CPU-only, static, /MD) once at configure time, then expose
# an INTERFACE target `PhysX::SDK` whose libs follow the consumer's $<CONFIG>.
#
# Inputs (cache):
#   PHYSX_SOURCE_DIR  : pristine PhysX "physx" dir (read-only is fine; it is copied because PhysX
#                       writes include/PxConfig.h into its own source tree at configure time)
#   PHYSX_CONFIGS     : PhysX configs to build (subset of debug;checked;profile;release)
# Output:
#   PhysX::SDK target (include dir, DISABLE_CUDA_PHYSX + PX_PHYSX_STATIC_LIB, per-config libs)
include_guard(GLOBAL)

set(PHYSX_SOURCE_DIR "" CACHE PATH "PhysX SDK 'physx' directory (contains CMakeLists.txt)")
set(PHYSX_CONFIGS "release;debug;profile" CACHE STRING "PhysX configs to build")
set(PHYSX_WORK_DIR "${CMAKE_BINARY_DIR}/_physx" CACHE PATH "PhysX bootstrap work dir")
option(PHYSX_FLOAT_POINT_PRECISE_MATH "Build PhysX with /fp:precise instead of /fp:fast" OFF)

if(NOT EXISTS "${PHYSX_SOURCE_DIR}/include/PxPhysicsAPI.h")
  message(FATAL_ERROR "PHYSX_SOURCE_DIR='${PHYSX_SOURCE_DIR}' is not a PhysX 'physx' directory")
endif()

set(_src "${PHYSX_WORK_DIR}/src")
set(_out "${PHYSX_WORK_DIR}/out")
if(NOT EXISTS "${_src}/CMakeLists.txt")
  message(STATUS "PhysX: copying ${PHYSX_SOURCE_DIR} -> ${_src}")
  file(COPY "${PHYSX_SOURCE_DIR}/" DESTINATION "${_src}"
       PATTERN "snippets/media" EXCLUDE PATTERN "documentation" EXCLUDE)
endif()

if(CMAKE_GENERATOR MATCHES "Visual Studio")
  set(_gen -G "${CMAKE_GENERATOR}" -A x64)
else()
  set(_gen -G Ninja)   # requires a vcvars64 environment, same as the parent Ninja build
endif()

foreach(_cfg IN LISTS PHYSX_CONFIGS)
  set(_bld "${PHYSX_WORK_DIR}/${_cfg}")  # keep short: PhysX object paths are deep (MAX_PATH 260)
  set(_stamp "${_out}/bin/win.x86_64.vc143.md/${_cfg}/PhysX_static_64.lib")
  if(EXISTS "${_stamp}")
    continue()
  endif()
  message(STATUS "PhysX: configuring + building '${_cfg}' (one-time, ~1 min) ...")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_src}" -B "${_bld}" ${_gen}
            -DCMAKE_BUILD_TYPE=${_cfg}
            -DPX_GENERATE_GPU_PROJECTS=OFF -DPX_GENERATE_STATIC_LIBRARIES=ON
            -DNV_USE_STATIC_WINCRT=OFF -DNV_USE_DEBUG_WINCRT=ON
            -DPX_BUILDSNIPPETS=OFF -DPX_BUILDPVDRUNTIME=OFF
            -DPX_FLOAT_POINT_PRECISE_MATH=${PHYSX_FLOAT_POINT_PRECISE_MATH}
            -DPX_OUTPUT_LIB_DIR=${_out} -DPX_OUTPUT_BIN_DIR=${_out}
    RESULT_VARIABLE _rc OUTPUT_QUIET)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "PhysX configure (${_cfg}) failed: ${_rc}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_bld}" --config ${_cfg} --parallel
                  RESULT_VARIABLE _rc OUTPUT_QUIET)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "PhysX build (${_cfg}) failed: ${_rc}")
  endif()
endforeach()

# Consumer config -> PhysX config. RelWithDebInfo -> profile (PX_SUPPORT_PVD=1, PX_PROFILE=1) if built.
set(_rwdi release)
if("profile" IN_LIST PHYSX_CONFIGS)
  set(_rwdi profile)
endif()
set(_dbg release)
if("debug" IN_LIST PHYSX_CONFIGS)
  set(_dbg debug)
endif()
set(_pxcfg "$<IF:$<CONFIG:Debug>,${_dbg},$<IF:$<CONFIG:RelWithDebInfo>,${_rwdi},release>>")
set(_libdir "${_out}/bin/win.x86_64.vc143.md/${_pxcfg}")

add_library(PhysXSDK INTERFACE)
add_library(PhysX::SDK ALIAS PhysXSDK)
target_include_directories(PhysXSDK SYSTEM INTERFACE "${_src}/include")
target_compile_definitions(PhysXSDK INTERFACE DISABLE_CUDA_PHYSX PX_PHYSX_STATIC_LIB)
foreach(_l PhysXExtensions PhysX PhysXPvdSDK PhysXCommon PhysXFoundation)
  target_link_libraries(PhysXSDK INTERFACE "${_libdir}/${_l}_static_64.lib")
endforeach()
