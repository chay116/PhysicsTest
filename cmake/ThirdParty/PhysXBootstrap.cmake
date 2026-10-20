# PhysXBootstrap.cmake - ALTERNATIVE PhysX path (02_Architecture 4.3): build PhysX 5.11 once at configure time in a
# separate tree and consume it only through an INTERFACE target, so PhysX cannot touch the parent's settings with
# any generator. Use it with Visual Studio generators (forced by PTToolchain.cmake) or when 03 M0 criterion 4
# fails (risk R6): -DPT_PHYSX_USE_BOOTSTRAP=ON.
#
# Differences from the prototype (prototypes/physx_superbuild): the source is our own pinned clone in the build tree
# (no copy of the mirror), the configuration map is Debug -> debug and Release -> release only (RelWithDebInfo is
# rejected by PTToolchain.cmake), and a missing debug library is a hard error instead of a silent release fallback.
include_guard(GLOBAL)
include(PtFetch)

pt_fetch_git_pinned(physx URL "${PT_PHYSX_GIT}" TAG "${PT_PHYSX_GIT_TAG}" PIN "${PT_PHYSX_PIN_SHA}")

set(_px_src  "${PT_PHYSX_SOURCE_DIR}/physx")
set(_px_work "${CMAKE_BINARY_DIR}/_physx")     # keep short: PhysX object paths are deep (MAX_PATH 260)
set(_px_out  "${_px_work}/out")

if(PT_MULTI_CONFIG)
	set(_px_configs debug release)
else()
	string(TOLOWER "${CMAKE_BUILD_TYPE}" _px_configs)  # Debug -> debug, Release -> release
endif()

if(CMAKE_GENERATOR MATCHES "Visual Studio")
	set(_px_gen -G "${CMAKE_GENERATOR}" -A x64)
else()
	set(_px_gen -G Ninja "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
		"-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}" "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
endif()

foreach(_cfg IN LISTS _px_configs)
	file(GLOB _px_stamp "${_px_out}/bin/*/${_cfg}/PhysX_static_64.lib")
	if(_px_stamp)
		continue()
	endif()
	message(STATUS "PhysXBootstrap: configuring + building '${_cfg}' (one-time, about 1 min) ...")
	execute_process(
		COMMAND "${CMAKE_COMMAND}" -S "${_px_src}" -B "${_px_work}/${_cfg}" ${_px_gen}
			-DCMAKE_BUILD_TYPE=${_cfg}
			-DPX_GENERATE_GPU_PROJECTS=OFF -DPX_GENERATE_STATIC_LIBRARIES=ON
			-DNV_USE_STATIC_WINCRT=OFF -DNV_USE_DEBUG_WINCRT=ON
			-DPX_BUILDSNIPPETS=OFF -DPX_BUILDPVDRUNTIME=OFF -DPX_FLOAT_POINT_PRECISE_MATH=OFF
			"-DPX_OUTPUT_LIB_DIR=${_px_out}" "-DPX_OUTPUT_BIN_DIR=${_px_out}"
		RESULT_VARIABLE _rc OUTPUT_QUIET)
	if(NOT _rc EQUAL 0)
		message(FATAL_ERROR "PhysXBootstrap: configure (${_cfg}) failed: ${_rc}")
	endif()
	execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_px_work}/${_cfg}" --config ${_cfg} --parallel
		RESULT_VARIABLE _rc OUTPUT_QUIET)
	if(NOT _rc EQUAL 0)
		message(FATAL_ERROR "PhysXBootstrap: build (${_cfg}) failed: ${_rc}")
	endif()
endforeach()

foreach(_cfg IN LISTS _px_configs)
	file(GLOB _px_libdir LIST_DIRECTORIES true "${_px_out}/bin/*/${_cfg}")
	if(NOT _px_libdir OR NOT EXISTS "${_px_libdir}/PhysX_static_64.lib")
		message(FATAL_ERROR "PhysXBootstrap: no '${_cfg}' PhysX libraries under ${_px_out}/bin (no fallback to another configuration)")
	endif()
	set(_px_libdir_${_cfg} "${_px_libdir}")
endforeach()

add_library(pt_physx INTERFACE)
add_library(PT::PhysX ALIAS pt_physx)
target_include_directories(pt_physx SYSTEM INTERFACE "${_px_src}/include")
target_compile_definitions(pt_physx INTERFACE PX_PHYSX_STATIC_LIB DISABLE_CUDA_PHYSX)
foreach(_lib PhysXExtensions PhysX PhysXPvdSDK PhysXCommon PhysXFoundation)
	foreach(_cfg IN LISTS _px_configs)
		if(_cfg STREQUAL "debug")
			target_link_libraries(pt_physx INTERFACE "$<$<CONFIG:Debug>:${_px_libdir_debug}/${_lib}_static_64.lib>")
		else()
			target_link_libraries(pt_physx INTERFACE "$<$<CONFIG:Release>:${_px_libdir_release}/${_lib}_static_64.lib>")
		endif()
	endforeach()
endforeach()
