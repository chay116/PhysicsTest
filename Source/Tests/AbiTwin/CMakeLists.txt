# PtBackendAbiTwin - a second static test backend for PhysicsTestAutomation (Kit state isolation, MXCSR / exception
# guards; see AbiTwinBackend.cpp). PT_BACKEND_NS=AbiTwin. Test-only: not in the backend registry.
add_library(PtBackendAbiTwin STATIC AbiTwinBackend.cpp)
target_link_libraries(PtBackendAbiTwin PRIVATE PT::Abi)
target_compile_definitions(PtBackendAbiTwin PRIVATE PT_BACKEND_NS=AbiTwin)
pt_target_defaults(PtBackendAbiTwin)
set_property(TARGET PtBackendAbiTwin PROPERTY FOLDER "PhysicsTest/Tests")
