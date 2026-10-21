# PtBullet.cmake - Bullet 3.25 (2c204c49) -> PT::Bullet (02_Architecture 4.3, DL54).
#
# Produces PT::Bullet: INTERFACE target with Bullet's src/ include dir, BulletDynamics / BulletCollision /
# LinearMath, and the header-visible BT_* definitions Bullet itself is built with (Bullet's libraries carry no usage
# requirements and its own BT_* definitions are add_definitions() in Bullet's directory only, so the harness sets one
# list on both sides - see _pt_bullet_header_defines below).
#
# Why each line exists (verified against bullet3 @ 2c204c49 with CMake 4.1.2):
#  * Bullet's CMakeLists.txt:1 says cmake_minimum_required(VERSION 2.4.3): CMake >= 4.0 refuses it unless
#    CMAKE_POLICY_VERSION_MINIMUM >= 3.5 is set (3.10: no deprecation warning).
#  * At that policy level CMP0077 is OLD inside Bullet, so option() ignores plain variables on the first configure.
#    Every Bullet option is therefore a CACHE ... FORCE entry.
#  * CMP0091 is decided by OUR top-level project() call, so CMAKE_MSVC_RUNTIME_LIBRARY (/MD) still governs Bullet.
#  * Single-argument FetchContent_Populate() is forbidden by CMP0169 (CMake >= 3.30): we populate through
#    pt_fetch_git_pinned() (SOURCE_SUBDIR without CMakeLists.txt) and add_subdirectory() ourselves.
#  * BT_THREADSAFE stays OFF (single-threaded world, D2). The MT variant is a separate backlog item (M15).
include_guard(GLOBAL)
include(PtFetch)

option(PT_BULLET_DOUBLE "BT_USE_DOUBLE_PRECISION (disables Bullet's SSE path on MSVC; backlog M15)" OFF)

pt_fetch_git_pinned(bullet3 URL "${PT_BULLET_GIT}" TAG "${PT_BULLET_GIT_TAG}" PIN "${PT_BULLET_PIN_SHA}")

macro(_pt_bullet_force Name Type Value)
	set(${Name} ${Value} CACHE ${Type} "Forced by PtBullet.cmake" FORCE)
endmacro()

# Rigid-body libraries only.
foreach(_opt BUILD_BULLET2_DEMOS BUILD_CPU_DEMOS BUILD_OPENGL3_DEMOS BUILD_EXTRAS BUILD_UNIT_TESTS
		BUILD_PYBULLET BUILD_BULLET3 BUILD_ENET BUILD_CLSOCKET USE_GRAPHICAL_BENCHMARK
		INSTALL_LIBS INSTALL_CMAKE_FILES USE_GLUT ENABLE_VHACD USE_OPENVR)
	_pt_bullet_force(${_opt} BOOL OFF)
endforeach()
_pt_bullet_force(USE_SOFT_BODY_MULTI_BODY_DYNAMICS_WORLD BOOL OFF)
_pt_bullet_force(USE_MSVC_RUNTIME_LIBRARY_DLL BOOL ON)    # no-op under our CMP0091 NEW; matches a standalone build
_pt_bullet_force(USE_MSVC_FAST_FLOATINGPOINT BOOL ON)     # /fp:fast (D5)
_pt_bullet_force(USE_DOUBLE_PRECISION BOOL ${PT_BULLET_DOUBLE})
_pt_bullet_force(BULLET2_MULTITHREADING BOOL OFF)         # BT_THREADSAFE=0 (the prototype default ON is changed)
_pt_bullet_force(BULLET2_USE_PPL_MULTITHREADING BOOL OFF)
_pt_bullet_force(BULLET2_USE_OPEN_MP_MULTITHREADING BOOL OFF)
_pt_bullet_force(BULLET2_USE_TBB_MULTITHREADING BOOL OFF)
# Options that add header-visible definitions through Bullet's add_definitions() (CMakeLists.txt:229-236): forced so a
# stray -D on the command line cannot give Bullet's TUs and the adapter TUs different inline code (btRigidBody.h:308-386
# uses BT_CLAMP_VELOCITY_TO). 0 / OFF = the definitions are not emitted at all.
_pt_bullet_force(CLAMP_VELOCITIES STRING 0)
_pt_bullet_force(INTERNAL_UPDATE_SERIALIZATION_STRUCTURES BOOL OFF)
# Bullet writes these as global CACHE variables (CMakeLists.txt:209-211); pre-seed them so the "_Debug" postfix
# does not leak into our own library targets.
set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "")
set(CMAKE_MINSIZEREL_POSTFIX "" CACHE STRING "")
set(CMAKE_RELWITHDEBINFO_POSTFIX "" CACHE STRING "")

if(BUILD_SHARED_LIBS)
	message(FATAL_ERROR "PhysicsTest links Bullet statically (Bullet has no dllexport); configure with BUILD_SHARED_LIBS=OFF")
endif()

block(SCOPE_FOR VARIABLES)
	set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
	add_subdirectory("${PT_BULLET3_SOURCE_DIR}" "${PT_BULLET3_BINARY_DIR}" EXCLUDE_FROM_ALL SYSTEM)
endblock()

foreach(_t LinearMath BulletCollision BulletDynamics)
	set_property(TARGET ${_t} PROPERTY FOLDER "ThirdParty/Bullet")
	# Korean (cp949) Windows: btGImpactShape.h / btLemkeAlgorithm.h carry Latin-1 bytes in author comments
	# -> C4819. Comment-only and harmless; silenced for Bullet's own TUs.
	target_compile_options(${_t} PRIVATE /wd4819)
endforeach()

# Header-visible BT_* definitions, identical on Bullet's own TUs and on every PT::Bullet consumer, so both compile the
# same inline code (ODR). BT_THREADSAFE is spelled out as 0: Bullet tests it with #if (btThreads.h and the TUs), so
# 0 equals "undefined" for Bullet, while a consumer's self-report can tell "exported 0" from "missing".
# DumpBuildFlags.cmake records the set ("bullet_header_defines") and flags any TU that disagrees.
set(_pt_bullet_header_defines BT_THREADSAFE=0)
if(PT_BULLET_DOUBLE)
	list(APPEND _pt_bullet_header_defines BT_USE_DOUBLE_PRECISION)
endif()
function(_pt_bullet_collect_targets Dir OutVar)
	get_property(_targets DIRECTORY "${Dir}" PROPERTY BUILDSYSTEM_TARGETS)
	get_property(_subdirs DIRECTORY "${Dir}" PROPERTY SUBDIRECTORIES)
	foreach(_sub IN LISTS _subdirs)
		_pt_bullet_collect_targets("${_sub}" _sub_targets)
		list(APPEND _targets ${_sub_targets})
	endforeach()
	set(${OutVar} ${_targets} PARENT_SCOPE)
endfunction()
_pt_bullet_collect_targets("${PT_BULLET3_SOURCE_DIR}" _pt_bullet_targets)
foreach(_t IN LISTS _pt_bullet_targets)
	get_target_property(_type ${_t} TYPE)
	if(_type MATCHES "^(STATIC|SHARED|OBJECT|MODULE)_LIBRARY$|^EXECUTABLE$")
		target_compile_definitions(${_t} PRIVATE ${_pt_bullet_header_defines})
	endif()
endforeach()
set(PT_BULLET_HEADER_DEFINES "${_pt_bullet_header_defines}" CACHE INTERNAL "BT_* definitions shared by Bullet and PT::Bullet consumers")
# MatchedAVX2 (M7, test-avx2): /arch on every Bullet target, PUBLIC like Jolt's, so the Bullet adapter and the smoke program
# (PT::Bullet consumers) compile Bullet's inline code with the libraries' ISA; harness TUs never see it (PTFlagProfiles.cmake).
if(PT_ARCH_BULLET)
	pt_apply_arch(ARCH ${PT_ARCH_BULLET} SCOPE PUBLIC TARGETS ${_pt_bullet_targets})
endif()

add_library(pt_bullet INTERFACE)
add_library(PT::Bullet ALIAS pt_bullet)
target_include_directories(pt_bullet SYSTEM INTERFACE "${PT_BULLET3_SOURCE_DIR}/src")
target_link_libraries(pt_bullet INTERFACE BulletDynamics BulletCollision LinearMath)
target_compile_definitions(pt_bullet INTERFACE ${_pt_bullet_header_defines})
