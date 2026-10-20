# PT::BackendCommon - header-only helpers shared by backends (02_Architecture 8.1, A.4).
# M0 ships the build self-report and the backend-side third-party include macros; the unit, combine, damping,
# contact and COM-wrapping headers (namespace PhysicsTest::Abi, inline namespace PT_BACKEND_NS) arrive in M2-M5.
# Backends see PT::Abi, PT::BackendCommon and their own SDK only (R4) - never harness Core.
pt_add_module(PtBackendCommon INTERFACE ALIAS PT::BackendCommon
	SOURCES
		Public/PtBuildReport.h
		Public/PtThirdPartyIncludes.h)
