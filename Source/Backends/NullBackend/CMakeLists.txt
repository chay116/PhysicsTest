# PT::Backend::Null (static) and PtBackend_Null.dll - the SDK-free reference backend (02_Architecture 7.7, A.13).
# Both link PT::BackendCommon (-> PT::Abi) only: no harness library (R4), so the Null frame planner is an independent
# re-implementation that gate A10 compares with the harness FStepPlanner. PT_BACKEND_NS=Null keeps the Kit symbols in
# their own inline namespace. The DLL imports only the C runtime and KERNEL32 (Tools/check_modules.py, A9).
set(_pt_null_sources
	Private/NullBackend.h
	Private/NullBackend.cpp)

add_library(PtBackendNull STATIC ${_pt_null_sources} Public/NullBackend/NullBackendEntry.h)
add_library(PT::Backend::Null ALIAS PtBackendNull)
target_include_directories(PtBackendNull PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_link_libraries(PtBackendNull PRIVATE PT::BackendCommon INTERFACE PT::Abi)
target_compile_definitions(PtBackendNull PRIVATE PT_BACKEND_NS=Null)
pt_target_defaults(PtBackendNull)
set_property(TARGET PtBackendNull PROPERTY FOLDER "PhysicsTest/Backends")

add_library(PtBackend_Null SHARED ${_pt_null_sources} Private/NullBackendDll.cpp)
target_include_directories(PtBackend_Null PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_link_libraries(PtBackend_Null PRIVATE PT::BackendCommon)
# PT_NULL_BACKEND_DLL: the DLL reads PT_NULL_DLL_NO_REINIT (the NO_REINIT test double, NullBackendDll.cpp).
target_compile_definitions(PtBackend_Null PRIVATE PT_BACKEND_NS=Null PT_NULL_BACKEND_DLL=1)
pt_target_defaults(PtBackend_Null)
set_property(TARGET PtBackend_Null PROPERTY FOLDER "PhysicsTest/Backends")
