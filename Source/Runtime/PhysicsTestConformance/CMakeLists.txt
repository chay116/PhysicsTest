# PT::Conformance - gates (02_Architecture 14, A.12). M2: the ABI gates A0-A7 and A10 over a backend's C table and
# FPhysScene. M3: the design's edges PT::Conformance -> PT::Scenarios (the free-fall fixture moved there, M3) and
# PT::BenchCore. M6 adds the physics gates G1, G1b, G2-G7, G9, G12 (Public/Gates, Private/Gates) over the gate scenes, with
# the tolerances of Config/Golden/tolerances.json (nlohmann-json, private). M10: G10b (the single-thread proof over the
# BenchCore thread census), the conformance stamp (writer and checker, 02 12.4) and the ABI call overhead on the TSC.
pt_add_module(PtConformance STATIC ALIAS PT::Conformance
	SOURCES
		Public/ConformanceGate.h
		Public/Abi/AbiGates.h
		Public/Gates/PhysicsGates.h
		Public/Gates/ThreadGate.h
		Public/ConformanceStamp.h
		Private/ConformanceGate.cpp
		Private/ConformanceStamp.cpp
		Private/Gates/ThreadGate.cpp
		Private/Gates/PhysicsGates.cpp
		Private/Gates/Tolerances.cpp
		Private/Abi/AbiFrameModelGate.cpp
		Private/Abi/AbiGateHelpers.h
		Private/Abi/AbiGates.cpp
	PUBLIC_DEPS PT::PhysicsCore PT::Abi PT::Scenarios PT::BenchCore
	PRIVATE_DEPS PT::Json)
