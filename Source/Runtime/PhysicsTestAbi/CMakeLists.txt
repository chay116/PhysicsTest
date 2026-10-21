# PT::Abi - the backend boundary (02_Architecture 7, A.1-A.3, docs/Abi.md): the pure-C ABI header PtBackendAbi.h (the
# only header shared with the Chaos DLL) and the header-only backend Kit PtBackendKit.h. Harness and backends see the
# same INTERFACE target; backends add PT_BACKEND_NS=<Engine> PRIVATE (Source/Backends).
pt_add_module(PtAbi INTERFACE ALIAS PT::Abi
	SOURCES
		Public/PhysicsTestAbi/PtBackendAbi.h
		Public/PhysicsTestAbi/PtBackendKit.h)

# The compile checks of 03 M2 criterion 1 (the ABI header as C11, the Kit with UE-like flags) live in their own
# directory: the UE-like check removes /EHsc from the directory-scoped CMAKE_CXX_FLAGS, and that must not reach any other
# target of this module (PtAbiCodec, M14).
add_subdirectory(Private/CompatCheck)
