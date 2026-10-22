// PhysXSceneConfig.h - FPhysXSceneConfig: the [PhysicsTest.Backend.PhysX] keys of a backend variant (02_Architecture 8.2
// profile table, A.14). [MVP M5]   PT_BACKEND_NS=PhysX
//
// Every key is a scene option (CreateScene validates it; the bench's preflight probe scene too) and has an evidence tag
// in the variant files (Config/Profiles/Backend/PhysX.*.ini), so the user can switch a UE4 behaviour after checking it
// in UE 4.27 without a code change. The defaults below are the UE4Compat column, so a run without a variant file is the
// main PhysX row. Lengths and velocities are in cm (cm/s); the backend converts with LengthScale. The substep clamp rule
// is not here: the harness FStepPlanner owns it (variant key [/Script/PhysicsTest.PhysicsTestSettings] SubstepClampRule).
#pragma once

#include "PhysXIncludes.h"
#include "PhysicsFiltering.h"
#include "PhysicsTestAbi/PtBackendKit.h"
#include "PtTypes.h"

namespace PhysicsTest::PhysXBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	enum class EPhysXContactOffsetRule : uint8
	{
		UE4Clamp,     // clamp(ContactOffsetMultiplier x MinExtent, MinContactOffset, MaxContactOffset) per element [M formula, V constants]
		SdkDefault,   // PhysX's own default: 0.02 x PxTolerancesScale::length (PxShape default)
	};

	enum class EPhysXStaticFrictionRule : uint8
	{
		EqualsDynamic,           // UE4: createMaterial(F, F, R) [M]
		MaxOfStaticAndDynamic,   // createMaterial(max(F, SF), F, R): PhysX then gives Chaos' pair rule (PxsMaterialCombiner.h [V])
	};

	enum class EPhysXThresholdRule : uint8
	{
		UE4Multiplier,   // the PhysX default threshold x the body's multiplier (UE4 InitDynamicProperties [M])
		SdkDefault,      // the PhysX default threshold as it is (5e-5 / 2.5e-5 x speed^2, PxvDynamics.h init [V])
	};

	enum class EPhysXDepenetrationRule : uint8
	{
		UE4Project,   // unset (-1) -> ProjectMaxDepenetrationVelocity; then 0 -> PX_MAX_F32 (UE4: 0 = unlimited) [M]
		SdkDefault,   // unset -> PhysX's default (unlimited); a body value > 0 is set, 0 is not representable (> 0 required)
	};

	// M6 (lead (e)): whether PT_BODY_GYROSCOPIC reaches PxRigidBodyFlag::eENABLE_GYROSCOPIC_FORCES. The flag first appeared in
	// PhysX 5.1 (the 5.11 SDK has it, PxRigidBody.h); UE4 shipped PhysX 3.4, which integrates no gyroscopic term, so the
	// UE4Compat family ignores the request (recorded in body.gyroscopic); SdkDefault honours the body's flag.
	enum class EPhysXGyroscopicRule : uint8
	{
		Ignore,     // UE4 on PhysX 3.4: no gyroscopic term [R: 3.4 has no such flag; V: 5.11 PxRigidBodyFlag::eENABLE_GYROSCOPIC_FORCES]
		FromBody,   // the body's PT_BODY_GYROSCOPIC (UE5's bGyroscopicTorqueEnabled)
	};

	enum class EPhysXDispatcherKind : uint8
	{
		Auto,           // T = 1: FPhysXCPUDispatcherSingleThread (UE4); T >= 2: PxDefaultCpuDispatcherCreate(T)
		SingleThread,   // FPhysXCPUDispatcherSingleThread whatever T is
		PxDefault,      // PxDefaultCpuDispatcherCreate(T = 1 ? 0 : T); T = 1 is the inline reference of the hash cross-check
	};

	// M9 (docs/Abi.md 9.4 engine mapping, 02_Architecture 8.2 "FConstraintInstance -> PxD6Joint"): the PhysX joint an ABI
	// ball / hinge becomes. Both are hard joints (no limit, drive, break force or projection).
	enum class EPhysXJointRecipe : uint8
	{
		D6,       // UE4 [M]: every FConstraintInstance is a PxD6Joint; linear X / Y / Z eLOCKED, ball = eTWIST / eSWING1 / eSWING2
		          // eFREE, hinge = eTWIST eFREE (D6 twist = rotation about the frame X axis, PxD6Joint.h:45 [V])
		Native,   // PxSphericalJoint (ball) / PxRevoluteJoint (hinge; revolute axis = frame X, PxRevoluteJoint.h:59 [V])
	};

	// Which ABI body becomes PhysX actor0. UE4 handed the actors over swapped [M] (02 8.2: PxD6JointCreate(SDK, actor2,
	// frame2, actor1, frame1)): actor0 = Body2 (the parent, ConstraintBone2), actor1 = Body1 (the child). PhysX anchors the
	// linear rows at actor1's frame and reports the constraint force on actor0 (ExtConstraintHelper.h:136-147,
	// DySolverConstraints.cpp:578-603, ScConstraintSim.cpp:148-154 [V]), so the order is a behaviour switch, not cosmetics.
	enum class EPhysXJointActorOrder : uint8
	{
		ParentFirst,   // actor0 = Body2 (world: NULL, frame = the world pose), actor1 = Body1 [M] UE4
		ChildFirst,    // actor0 = Body1, actor1 = Body2
	};

	// Debug visualization categories (PxVisualizationParameter) of viz runs (PtSceneDesc::RunMode == PT_RUN_VIZ only; never in
	// bench or behaviour runs). Lengths are the harness's, in cm, converted with LengthScale (PhysXDebugRender.cpp).
	enum EPhysXVisualizationCategory : uint32
	{
		PhysXViz_CollisionShapes = 1u << 0,    // eCOLLISION_SHAPES
		PhysXViz_CollisionAABBs = 1u << 1,     // eCOLLISION_AABBS
		PhysXViz_ActorAxes = 1u << 2,          // eACTOR_AXES
		PhysXViz_BodyMassAxes = 1u << 3,       // eBODY_MASS_AXES (the inertia box; red = asleep)
		PhysXViz_BodyLinearVelocity = 1u << 4, // eBODY_LIN_VELOCITY
		PhysXViz_JointLocalFrames = 1u << 5,   // eJOINT_LOCAL_FRAMES (needs PxConstraintFlag::eVISUALIZATION on the joint)
		PhysXViz_JointLimits = 1u << 6,        // eJOINT_LIMITS
		PhysXViz_ContactPoints = 1u << 7,      // eCONTACT_POINT
		PhysXViz_ContactNormals = 1u << 8,     // eCONTACT_NORMAL
		PhysXViz_All = (1u << 9) - 1u,
	};
	// [D] the default of every PhysX variant file (DebugVisualization=...): the engine's own shapes, joint frames and contact
	// normals. ContactPoints (three 20 cm cross lines per point) is left out of the default: the host already draws the snapshot's
	// contact points, and a pile's crosses would triple the commands of every viewer frame.
	inline constexpr uint32 GPhysXDefaultVisualization = PhysXViz_CollisionShapes | PhysXViz_JointLocalFrames | PhysXViz_JointLimits | PhysXViz_ContactNormals;

	struct FPhysXSceneConfig
	{
		double LengthScale = 1.0;                                            // D1: native units per cm (1 = cm, 0.01 = m)
		physx::PxBroadPhaseType::Enum BroadPhase = physx::PxBroadPhaseType::eSAP;   // [M] UE4; 5.11 default ePABP
		physx::PxSolverType::Enum Solver = physx::PxSolverType::ePGS;              // [V]
		bool bEnablePCM = true;                                              // [V] PhysicsSettingsCore.cpp:27-49
		bool bEnableStabilization = false;                                   // [V]
		bool bEnableEnhancedDeterminism = false;                             // [V] (or PT_SIM_ENHANCED_DETERMINISM)
		bool bEnableCCD = true;                                              // [V] bDisableCCD false (PhysicsSettings.h:305); [M] -> eENABLE_CCD
		bool bEnableActiveActors = true;                                     // [V] bDisableActiveActors(false) (PhysicsSettings.cpp:18); [M] ->
		                                                                     // eENABLE_ACTIVE_ACTORS | eEXCLUDE_KINEMATICS_FROM_ACTIVE_ACTORS
		physx::PxPairFilteringMode::Enum KineKineFiltering = physx::PxPairFilteringMode::eKEEP;     // [V] bDisableKinematic*Pairs false
		                                                                     // (PhysicsSettings.h:296, :300); [M] kept, the shader suppresses
		physx::PxPairFilteringMode::Enum StaticKineFiltering = physx::PxPairFilteringMode::eKEEP;   // [M]
		EPhysXNonBlockingPairFilter NonBlockingPairFilter = EPhysXNonBlockingPairFilter::Kill;                         // [M] PhysicsFiltering.h
		EPhysXKinematicStaticPairFilter KinematicStaticPairFilter = EPhysXKinematicStaticPairFilter::UnlessKinematicPairs;   // [M]
		double BounceThresholdVelocityCm = 200.0;                            // [V] cm/s; Desc.RestitutionThreshold > 0 wins
		uint32 DynamicTreeRebuildRateHint = 10;                              // [V] PhysXTreeRebuildRate
		uint32 ScratchMemoryBytes = 262144;                                  // [V] SimulateScratchMemorySize; 0 = none
		uint32 PositionIterations = 8;                                       // [M] UE4 FBodyInstance 8/1 (UE5: 8/2)
		uint32 VelocityIterations = 1;
		EPhysXContactOffsetRule ContactOffsetRule = EPhysXContactOffsetRule::UE4Clamp;
		double ContactOffsetMultiplier = 0.02;                               // [V] PhysicsSettingsCore.cpp:27-49
		double MinContactOffsetCm = 2.0;                                     // [V]
		double MaxContactOffsetCm = 8.0;                                     // [V]
		EPhysXStaticFrictionRule StaticFrictionRule = EPhysXStaticFrictionRule::EqualsDynamic;
		EPhysXThresholdRule ThresholdRule = EPhysXThresholdRule::UE4Multiplier;
		EPhysXDepenetrationRule DepenetrationRule = EPhysXDepenetrationRule::UE4Project;
		double ProjectMaxDepenetrationVelocityCm = 0.0;                      // [V] zero-initialised, "0 means no maximum" (PhysicsSettingsCore.h:93-95)
		EPhysXDispatcherKind Dispatcher = EPhysXDispatcherKind::Auto;
		EPhysXGyroscopicRule GyroscopicRule = EPhysXGyroscopicRule::Ignore;   // UE4Compat column (M6)
		EPhysXJointRecipe JointRecipe = EPhysXJointRecipe::D6;               // [M] UE4 FConstraintInstance -> PxD6Joint (M9)
		EPhysXJointActorOrder JointActorOrder = EPhysXJointActorOrder::ParentFirst;   // [M] UE4 swapped the actors (M9)
		uint32 Visualization = GPhysXDefaultVisualization;                    // [D] EPhysXVisualizationCategory bits, viz runs only (M9)
	};

	// One [PhysicsTest.Backend.PhysX] key, case-insensitive. PT_OK, or PT_E_INVALID_ARG with a message in Error.
	PtResult ApplySceneOption(FPhysXSceneConfig& Config, ::PhysicsTest::Abi::FStringRef Key, ::PhysicsTest::Abi::FStringRef Value, char (&Error)[256]);

	// Keys a running scene accepts through SetOption (the others need CreateScene).
	bool IsRuntimeSceneOption(::PhysicsTest::Abi::FStringRef Key);

	const char* ToString(physx::PxBroadPhaseType::Enum Value);
	const char* ToString(physx::PxSolverType::Enum Value);
	const char* ToString(physx::PxPairFilteringMode::Enum Value);
	const char* ToString(EPhysXContactOffsetRule Value);
	const char* ToString(EPhysXStaticFrictionRule Value);
	const char* ToString(EPhysXThresholdRule Value);
	const char* ToString(EPhysXDepenetrationRule Value);
	const char* ToString(EPhysXDispatcherKind Value);
	const char* ToString(EPhysXGyroscopicRule Value);
	const char* ToString(EPhysXJointRecipe Value);
	const char* ToString(EPhysXJointActorOrder Value);
	// The variant-file name of one visualization category bit ("CollisionShapes", ...), "?" for anything else.
	const char* GetVisualizationCategoryName(uint32 CategoryBit);
}
