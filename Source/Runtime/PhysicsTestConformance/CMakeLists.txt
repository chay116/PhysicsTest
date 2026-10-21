# PT::Conformance - gates (02_Architecture 14, A.12). M2: the ABI gates A0-A7 and A10 over a backend's C table and
# FPhysScene. M3: the design's edges PT::Conformance -> PT::Scenarios (the free-fall fixture moved there, M3) and
# PT::BenchCore. M6 adds the G gates and the gate scenes.
pt_add_module(PtConformance STATIC ALIAS PT::Conformance
	SOURCES
		Public/ConformanceGate.h
		Public/Abi/AbiGates.h
		Private/ConformanceGate.cpp
		Private/Abi/AbiFrameModelGate.cpp
		Private/Abi/AbiGateHelpers.h
		Private/Abi/AbiGates.cpp
	PUBLIC_DEPS PT::PhysicsCore PT::Abi PT::Scenarios PT::BenchCore)
