# PT::BackendCommon - header-only helpers shared by backends (02_Architecture 8.1, A.4).
# M0: the build self-report and the backend-side third-party include macros. M2 (skeleton): fixed-width types, unit
# scale + capsule axis remap, combine rules, damping conversion, contact convention + FNV-1a. M3 adds the COM-wrapping
# rule (PtShapeWrapping.h), M5 the BackendCommon test set. Every header except PtTypes / PtBuildReport /
# PtThirdPartyIncludes needs PT_BACKEND_NS (namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS }).
# Backends see PT::Abi, PT::BackendCommon and their own SDK only (R4) - never harness Core.
pt_add_module(PtBackendCommon INTERFACE ALIAS PT::BackendCommon
	SOURCES
		Public/PtBuildReport.h
		Public/PtCombine.h
		Public/PtContactConvention.h
		Public/PtDamping.h
		Public/PtThirdPartyIncludes.h
		Public/PtTypes.h
		Public/PtUnitScale.h
	INTERFACE_DEPS PT::Abi)
