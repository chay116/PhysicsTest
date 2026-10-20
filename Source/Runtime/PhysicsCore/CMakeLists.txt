# PT::PhysicsCore - the harness data model (02_Architecture 5-6, 9.2, 10; A.6-A.10): UE-named types with UE defaults,
# BodyUtils (harness mass properties, D5), FStepPlanner (all tick modes), the profile resolver and the FPhysScene
# skeleton (M1, stub backend). M2 adds the host side of the C ABI (FPhysicsBackendScene, FPhysicsInterface).
# Backends never link this module (R4): they see PT::Abi, PT::BackendCommon and their own SDK only.
pt_add_module(PtPhysicsCore STATIC ALIAS PT::PhysicsCore
	SOURCES
		Public/PhysScene.h
		Public/Physics/AnalyticReference.h
		Public/Physics/PhysicsConsoleVariables.h
		Public/Physics/PhysicsInterfaceTypes.h
		Public/Physics/PhysicsProfile.h
		Public/Physics/PhysSceneStats.h
		Public/Physics/StepPlanner.h
		Public/PhysicalMaterials/PhysicalMaterial.h
		Public/PhysicsEngine/AggregateGeom.h
		Public/PhysicsEngine/BodyInstance.h
		Public/PhysicsEngine/BodySetup.h
		Public/PhysicsEngine/BodyUtils.h
		Public/PhysicsEngine/BoxElem.h
		Public/PhysicsEngine/ConstraintInstance.h
		Public/PhysicsEngine/ConstraintUtils.h
		Public/PhysicsEngine/ConvexElem.h
		Public/PhysicsEngine/PhysicsSettings.h
		Public/PhysicsEngine/ShapeElem.h
		Public/PhysicsEngine/SphereElem.h
		Public/PhysicsEngine/SphylElem.h
		Private/PhysScene.cpp
		Private/Physics/AnalyticReference.cpp
		Private/Physics/Matrix33.cpp
		Private/Physics/Matrix33.h
		Private/Physics/PhysicsConsoleVariables.cpp
		Private/Physics/PhysicsProfile.cpp
		Private/Physics/StepPlanner.cpp
		Private/PhysicalMaterials/PhysicalMaterial.cpp
		Private/PhysicsEngine/AggregateGeom.cpp
		Private/PhysicsEngine/BodyInstance.cpp
		Private/PhysicsEngine/BodyUtils.cpp
		Private/PhysicsEngine/ConstraintInstance.cpp
		Private/PhysicsEngine/PhysicsSettings.cpp
	PUBLIC_DEPS PT::Core)
