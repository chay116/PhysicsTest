# PtBullet.cmake -- consume Bullet 3.25 (source tree) via add_subdirectory.
#
# Produces:  PT::Bullet  (INTERFACE target: include dir + BulletDynamics/BulletCollision/LinearMath
#                         + the same compile definitions Bullet itself was built with)
#
# Why each line exists (verified against bullet3 @ 2c204c49 with CMake 4.1.2):
#  * Bullet's CMakeLists.txt:1 says cmake_minimum_required(VERSION 2.4.3) -> CMake >= 4.0 hard-errors
#    unless CMAKE_POLICY_VERSION_MINIMUM >= 3.5 is set (3.10 used: no deprecation warning, same OLD CMP0077/CMP0091).
#  * Under that policy level (<3.13) CMP0077 is OLD, so Bullet's option() IGNORES normal variables on the first
#    configure and is shadowed by them on re-configure. Every Bullet option must be a CACHE ... FORCE entry.
#  * Bullet's libraries carry no usage requirements (no target_include_directories, and static libs do not
#    link each other), and BT_THREADSAFE / BT_USE_DOUBLE_PRECISION are add_definitions() in Bullet's
#    directory scope only -> we re-export them through PT::Bullet so headers see the same ABI.
#  * With our top-level CMP0091 NEW the CRT is chosen by CMAKE_MSVC_RUNTIME_LIBRARY (USE_MSVC_RUNTIME_LIBRARY_DLL
#    becomes a no-op); we still set it ON so a standalone Bullet build matches (/MD).

include_guard(GLOBAL)

set(PT_BULLET_ROOT "D:/PhysicsRefs/bullet3" CACHE PATH "Bullet3 source tree (3.25)")
option(PT_BULLET_MULTITHREADING "Build Bullet with BT_THREADSAFE=1 (enables btDiscreteDynamicsWorldMt)" ON)
option(PT_BULLET_USE_PPL        "Also compile Bullet's PPL task scheduler (MSVC only)" ON)
option(PT_BULLET_USE_OPENMP     "Also compile Bullet's OpenMP task scheduler (/openmp)" OFF)
option(PT_BULLET_DOUBLE         "BT_USE_DOUBLE_PRECISION (disables Bullet's SSE path on MSVC)" OFF)
option(PT_BULLET_FAST_MATH      "Keep Bullet's default /fp:fast (USE_MSVC_FAST_FLOATINGPOINT)" ON)

if(NOT EXISTS "${PT_BULLET_ROOT}/src/btBulletDynamicsCommon.h")
  message(FATAL_ERROR "PT_BULLET_ROOT='${PT_BULLET_ROOT}' is not a Bullet3 source tree")
endif()

macro(_pt_bullet_force name type value)
  set(${name} ${value} CACHE ${type} "Forced by PtBullet.cmake" FORCE)
endmacro()

# --- strip everything except the rigid-body libraries --------------------------------------------
foreach(_opt BUILD_BULLET2_DEMOS BUILD_CPU_DEMOS BUILD_OPENGL3_DEMOS BUILD_EXTRAS BUILD_UNIT_TESTS
             BUILD_PYBULLET BUILD_BULLET3 BUILD_ENET BUILD_CLSOCKET USE_GRAPHICAL_BENCHMARK
             INSTALL_LIBS INSTALL_CMAKE_FILES USE_GLUT ENABLE_VHACD USE_OPENVR)
  _pt_bullet_force(${_opt} BOOL OFF)
endforeach()
_pt_bullet_force(USE_SOFT_BODY_MULTI_BODY_DYNAMICS_WORLD BOOL OFF)
_pt_bullet_force(USE_MSVC_RUNTIME_LIBRARY_DLL BOOL ON)
_pt_bullet_force(USE_MSVC_FAST_FLOATINGPOINT BOOL ${PT_BULLET_FAST_MATH})
_pt_bullet_force(USE_DOUBLE_PRECISION BOOL ${PT_BULLET_DOUBLE})
_pt_bullet_force(BULLET2_MULTITHREADING BOOL ${PT_BULLET_MULTITHREADING})
if(PT_BULLET_MULTITHREADING)
  _pt_bullet_force(BULLET2_USE_PPL_MULTITHREADING BOOL ${PT_BULLET_USE_PPL})
  _pt_bullet_force(BULLET2_USE_OPEN_MP_MULTITHREADING BOOL ${PT_BULLET_USE_OPENMP})
  _pt_bullet_force(BULLET2_USE_TBB_MULTITHREADING BOOL OFF)
else()
  _pt_bullet_force(BULLET2_USE_PPL_MULTITHREADING BOOL OFF)
  _pt_bullet_force(BULLET2_USE_OPEN_MP_MULTITHREADING BOOL OFF)
endif()
# Bullet writes these as global CACHE vars (CMakeLists.txt:201,209-211); pre-seed them so the
# "_Debug"/"_MinsizeRel"/"_RelWithDebugInfo" postfixes do not leak into our own library targets.
# (LIBRARY_OUTPUT_PATH also leaks, but CMAKE_ARCHIVE/LIBRARY/RUNTIME_OUTPUT_DIRECTORY set at top level win.)
set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "")
set(CMAKE_MINSIZEREL_POSTFIX "" CACHE STRING "")
set(CMAKE_RELWITHDEBINFO_POSTFIX "" CACHE STRING "")

# BUILD_SHARED_LIBS: Bullet has no __declspec(dllexport) -> must stay static on MSVC.
# (Do not set it as a normal variable here: CMP0077 OLD makes Bullet's option() clear/ignore it.)
if(BUILD_SHARED_LIBS)
  message(FATAL_ERROR "PhysicsTest links Bullet statically; configure with BUILD_SHARED_LIBS=OFF")
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)  # 3.5 also works but triggers the "< 3.10 deprecated" warning
add_subdirectory("${PT_BULLET_ROOT}" "${CMAKE_BINARY_DIR}/_deps/bullet3" EXCLUDE_FROM_ALL SYSTEM)
unset(CMAKE_POLICY_VERSION_MINIMUM)

foreach(_t LinearMath BulletCollision BulletDynamics)
  set_target_properties(${_t} PROPERTIES FOLDER "ThirdParty/Bullet")
  # Korean (cp949) Windows: btGImpactShape.h / btLemkeAlgorithm.h have Latin-1 bytes in author comments
  # -> C4819 (cp949) or C4828 (/utf-8). Comment-only, harmless; silence it for Bullet's own TUs.
  target_compile_options(${_t} PRIVATE /wd4819)
endforeach()

add_library(pt_bullet INTERFACE)
add_library(PT::Bullet ALIAS pt_bullet)
target_include_directories(pt_bullet SYSTEM INTERFACE "${PT_BULLET_ROOT}/src")
target_link_libraries(pt_bullet INTERFACE BulletDynamics BulletCollision LinearMath)
target_compile_definitions(pt_bullet INTERFACE
  PT_WITH_BULLET=1
  $<$<BOOL:${PT_BULLET_DOUBLE}>:BT_USE_DOUBLE_PRECISION>
  $<$<BOOL:${PT_BULLET_MULTITHREADING}>:BT_THREADSAFE=1>
  $<$<AND:$<BOOL:${PT_BULLET_MULTITHREADING}>,$<BOOL:${PT_BULLET_USE_PPL}>>:BT_USE_PPL=1>
  $<$<AND:$<BOOL:${PT_BULLET_MULTITHREADING}>,$<BOOL:${PT_BULLET_USE_OPENMP}>>:BT_USE_OPENMP=1>)
