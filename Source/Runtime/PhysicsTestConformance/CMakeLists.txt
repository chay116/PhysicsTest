# PT::Conformance - gates (02_Architecture 14, A.12). M2: the ABI gates A0-A7 and A10 over a backend's C table and
# FPhysScene, plus the free-fall fixture (reused by M3 S00.FreeFall and the M2.5 spike's SP3). M6 adds the G gates and
# the gate scenes; the design's PT::Conformance -> PT::Scenarios / PT::BenchCore edges arrive with those targets (M3).
pt_add_module(PtConformance STATIC ALIAS PT::Conformance
	SOURCES
		Public/ConformanceGate.h
		Public/Abi/AbiGates.h
		Public/Fixtures/FreeFallFixture.h
		Private/ConformanceGate.cpp
		Private/Abi/AbiFrameModelGate.cpp
		Private/Abi/AbiGateHelpers.h
		Private/Abi/AbiGates.cpp
		Private/Fixtures/FreeFallFixture.cpp
	PUBLIC_DEPS PT::PhysicsCore PT::Abi)
