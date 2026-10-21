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

	enum class EPhysXDispatcherKind : uint8
	{
		Auto,           // T = 1: FPhysXCPUDispatcherSingleThread (UE4); T >= 2: PxDefaultCpuDispatcherCreate(T)
		SingleThread,   // FPhysXCPUDispatcherSingleThread whatever T is
		PxDefault,      // PxDefaultCpuDispatcherCreate(T = 1 ? 0 : T); T = 1 is the inline reference of the hash cross-check
	};

	struct FPhysXSceneConfig
	{
		double LengthScale = 1.0;                                            // D1: native units per cm (1 = cm, 0.01 = m)
		physx::PxBroadPhaseType::Enum BroadPhase = physx::PxBroadPhaseType::eSAP;   // [M] UE4; 5.11 default ePABP
		physx::PxSolverType::Enum Solver = physx::PxSolverType::ePGS;              // [V]
		bool bEnablePCM = true;                                              // [V] PhysicsSettingsCore.cpp:27-49
		bool bEnableStabilization = false;                                   // [V]
		bool bEnableEnhancedDeterminism = false;                             // [V] (or PT_SIM_ENHANCED_DETERMINISM)
		bool bEnableCCD = true;                                              // [M] scene flag eENABLE_CCD
		bool bEnableActiveActors = true;                                     // [M] eENABLE_ACTIVE_ACTORS | eEXCLUDE_KINEMATICS_FROM_ACTIVE_ACTORS
		physx::PxPairFilteringMode::Enum KineKineFiltering = physx::PxPairFilteringMode::eKEEP;     // [M] (the shader suppresses)
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
		double ProjectMaxDepenetrationVelocityCm = 0.0;                      // [V] UPhysicsSettingsCore default 0 (0 = unlimited in UE4)
		EPhysXDispatcherKind Dispatcher = EPhysXDispatcherKind::Auto;
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
}
