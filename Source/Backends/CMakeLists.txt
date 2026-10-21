# Source/Backends - one directory per engine backend (02_Architecture 3, 4.5, 8). Each backend links PT::BackendCommon
# (-> PT::Abi) and its own SDK PRIVATE, with PRIVATE PT_BACKEND_NS=<Engine>. M2: Null. M3: Jolt. M5: Bullet, PhysX.
#
# PT::BackendRegistry is generated here, after every backend target exists: PTBackendRegistry.gen.cpp references each
# static entry (PT_GetBackendApi_<Name>) directly, so no WHOLE_ARCHIVE is needed (02 4.5), and lists the DLL backends
# the host may load (PtBackend_Null.dll for the DLL-path tests; Chaos from M2.5 / M13).
add_subdirectory(NullBackend)
if(PT_WITH_JOLT)
	add_subdirectory(JoltBackend)
endif()
if(PT_WITH_BULLET)
	add_subdirectory(BulletBackend)
endif()
if(PT_WITH_PHYSX)
	add_subdirectory(PhysXBackend)
endif()

# Static backends: name;entry symbol;CMake target. DLL backends: name;file name (resolved next to the executable).
set(PT_STATIC_BACKENDS "Null;PT_GetBackendApi_Null;PT::Backend::Null")
if(PT_WITH_JOLT)
	list(APPEND PT_STATIC_BACKENDS "Jolt;PT_GetBackendApi_Jolt;PT::Backend::Jolt")
endif()
# M5: Bullet and PhysX join the registry when their own CMakeLists.txt sets the target property PT_IN_BACKEND_REGISTRY ON
# (OFF while the backend is the M5 stub whose CreateScene returns PT_E_UNSUPPORTED). Every registered SDK backend runs
# through the shared suites unchanged (SdkBackendTests, SdkBackendsA0toA7, ScenarioTests) and appears in the bench and the
# viewer, so a backend that cannot create a scene yet stays out. PhysicsTest.<Engine>Backend.Registered checks both states.
if(PT_WITH_BULLET)
	get_target_property(_pt_in_registry PtBackendBullet PT_IN_BACKEND_REGISTRY)
	if(_pt_in_registry)
		list(APPEND PT_STATIC_BACKENDS "Bullet;PT_GetBackendApi_Bullet;PT::Backend::Bullet")
	endif()
endif()
if(PT_WITH_PHYSX)
	get_target_property(_pt_in_registry PtBackendPhysX PT_IN_BACKEND_REGISTRY)
	if(_pt_in_registry)
		list(APPEND PT_STATIC_BACKENDS "PhysX;PT_GetBackendApi_PhysX;PT::Backend::PhysX")
	endif()
endif()
set(PT_DLL_BACKENDS "NullDll;PtBackend_Null.dll")

set(_pt_registry_decls "")
set(_pt_registry_static "")
set(_pt_registry_targets "")
list(LENGTH PT_STATIC_BACKENDS _pt_n)
math(EXPR _pt_last "${_pt_n} - 1")
foreach(_i RANGE 0 ${_pt_last} 3)
	math(EXPR _pt_i1 "${_i} + 1")
	math(EXPR _pt_i2 "${_i} + 2")
	list(GET PT_STATIC_BACKENDS ${_i} _pt_name)
	list(GET PT_STATIC_BACKENDS ${_pt_i1} _pt_entry)
	list(GET PT_STATIC_BACKENDS ${_pt_i2} _pt_target)
	string(APPEND _pt_registry_decls "extern \"C\" const PtBackendApi* PT_CALL ${_pt_entry}(uint32_t RequestedAbiMajor);\n")
	string(APPEND _pt_registry_static "\t\t\t{ \"${_pt_name}\", &${_pt_entry}, nullptr },\n")
	list(APPEND _pt_registry_targets ${_pt_target})
endforeach()
set(_pt_registry_dll "")
list(LENGTH PT_DLL_BACKENDS _pt_n)
math(EXPR _pt_last "${_pt_n} - 1")
foreach(_i RANGE 0 ${_pt_last} 2)
	math(EXPR _pt_i1 "${_i} + 1")
	list(GET PT_DLL_BACKENDS ${_i} _pt_name)
	list(GET PT_DLL_BACKENDS ${_pt_i1} _pt_file)
	string(APPEND _pt_registry_dll "\t\t\t{ \"${_pt_name}\", nullptr, L\"${_pt_file}\" },\n")
endforeach()
set(PT_REGISTRY_DECLARATIONS "${_pt_registry_decls}")
set(PT_REGISTRY_ENTRIES "${_pt_registry_static}${_pt_registry_dll}")
configure_file("${PROJECT_SOURCE_DIR}/cmake/PTBackendRegistry.cpp.in" "${PROJECT_BINARY_DIR}/Generated/PTBackendRegistry.gen.cpp" @ONLY)

add_library(PtBackendRegistry STATIC "${PROJECT_BINARY_DIR}/Generated/PTBackendRegistry.gen.cpp")
add_library(PT::BackendRegistry ALIAS PtBackendRegistry)
target_link_libraries(PtBackendRegistry PUBLIC PT::PhysicsCore PRIVATE ${_pt_registry_targets})
pt_target_defaults(PtBackendRegistry)
set_property(TARGET PtBackendRegistry PROPERTY FOLDER "PhysicsTest/Runtime")
