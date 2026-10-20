// PhysicsSettings.h - project physics settings and the harness settings (UE: PhysicsCore/Public/PhysicsSettingsCore.h,
// Engine/Classes/PhysicsEngine/PhysicsSettings.h, Chaos/Public/ChaosSolverConfiguration.h). [MVP M1]
// Every UE key, core fields included, lives in [/Script/Engine.PhysicsSettings] as in a UE game (BaseEngine.ini:3406 [V];
// UPhysicsSettings derives from UPhysicsSettingsCore, DL44). The two structs only mirror UE's class split; the harness
// never reads a [/Script/PhysicsCore.PhysicsSettingsCore] section. Harness-only settings are FPhysicsTestSettings in
// [/Script/PhysicsTest.PhysicsTestSettings]. The ini keys and their resolution are in Physics/PhysicsProfile.h.
#pragma once

#include "CoreTypes.h"
#include "Math/UnrealMathUtility.h"
#include "Physics/PhysicsInterfaceTypes.h"

namespace PhysicsTest
{
	// UE: FChaosSolverConfiguration (ChaosSolverConfiguration.cpp:8-26, defaults from FPBDRigidsEvolutionGBF
	// PBDRigidsEvolutionGBF.h:62-69 [V]). Phase-1 subset: destruction / clustering / generated-data fields are omitted.
	struct FChaosSolverConfiguration
	{
		int32 PositionIterations = 8;
		int32 VelocityIterations = 2;
		int32 ProjectionIterations = 1;
		float CollisionMarginFraction = 0.05f;
		float CollisionMarginMax = 10.0f;                         // cm
		float CollisionCullDistance = 3.0f;                       // cm
		float CollisionMaxPushOutVelocity = 1000.0f;              // cm/s
		float CollisionInitialOverlapDepenetrationVelocity = -1.0f;   // cm/s, < 0 = solver default
	};

	// UE: EChaosThreadingMode (Chaos/Public/PhysicsCoreTypes.h:20 [V]). The bench maps T instead (A.16).
	enum class EChaosThreadingMode : uint8
	{
		DedicatedThread,
		TaskGraph,
		SingleThread,
	};

	// UE: FChaosPhysicsSettings (PhysicsSettings.cpp:197-203 [V]); only the threading model is kept.
	struct FChaosPhysicsSettings
	{
		EChaosThreadingMode DefaultThreadingModel = EChaosThreadingMode::TaskGraph;
	};

	// UE: UPhysicsSettingsCore (PhysicsSettingsCore.cpp:27-49 [V]); Phase-1 subset of the config fields.
	struct FPhysicsSettingsCore
	{
		float DefaultGravityZ = -980.0f;              // cm/s^2
		int32 SimulateScratchMemorySize = 262144;     // bytes (PhysX era; read by PhysX UE4Compat)
		bool bEnableEnhancedDeterminism = false;
		bool bEnableShapeSharing = false;
		bool bEnablePCM = true;
		bool bEnableStabilization = false;
		float BounceThresholdVelocity = 200.0f;       // cm/s (PhysX era)
		EFrictionCombineMode FrictionCombineMode = EFrictionCombineMode::Average;
		EFrictionCombineMode RestitutionCombineMode = EFrictionCombineMode::Average;
		float MaxAngularVelocity = 3600.0f;           // deg/s (10 revolutions per second = 62.83 rad/s)
		float MaxDepenetrationVelocity = 0.0f;        // cm/s, 0 = unlimited
		float ContactOffsetMultiplier = 0.02f;        // PhysX era contact offset rule
		float MinContactOffset = 2.0f;                // cm
		float MaxContactOffset = 8.0f;                // cm
		FChaosSolverConfiguration SolverOptions;
	};

	// UE: UPhysicsSettings (PhysicsSettings.cpp:14-38 [V]); Phase-1 subset of the config fields.
	struct FPhysicsSettings : FPhysicsSettingsCore
	{
		float MinPhysicsDeltaTime = UE_SMALL_NUMBER;   // s
		float MaxPhysicsDeltaTime = 1.0f / 30.0f;      // s
		bool bSubstepping = false;                     // the harness tick mode is FPhysicsTestSettings::TickMode
		bool bTickPhysicsAsync = false;                // idem
		float AsyncFixedTimeStepSize = 1.0f / 30.0f;   // s
		float MaxSubstepDeltaTime = 1.0f / 60.0f;      // s
		int32 MaxSubsteps = 6;
		int32 PhysXTreeRebuildRate = 10;               // PhysX era (dynamicTreeRebuildRateHint)
		FChaosPhysicsSettings ChaosSettings;
	};

	// ---- Harness settings ([/Script/PhysicsTest.PhysicsTestSettings], 02_Architecture 9-10) ------------------------
	// How each frame becomes engine steps (FStepPlanner, 02 9.2). MVP scenes use FixedStep and Sync.
	enum class EPhysicsTickMode : uint8
	{
		FixedStep,     // N = 1, FixedDeltaTime; the frame dt is ignored (bench default)
		Sync,          // N = 1, min(dt, MaxPhysicsDeltaTime) (FChaosScene::SetUpForFrame)
		Substepping,   // UWorld bSubstepping; clamp rule = SubstepClampRule
		AsyncFixed,    // UWorld bTickPhysicsAsync: fixed AsyncFixedTimeStepSize steps from an accumulator
	};

	// Which substep formula the harness applies (owned by FStepPlanner; never sent to a backend, DL46).
	enum class ESubstepClampRule : uint8
	{
		UE5ChaosScene,    // FChaosScene + FPhysicsSolverBase::AdvanceAndDispatch_External (UE5)
		UE4PhysSubstep,   // UE4 FPhysSubstepTask::UpdateTime (dead code in 5.8.3), PhysX UE4Compat
	};

	// Auto: Native when the backend declares PT_CAP_NATIVE_FRAME_MODEL (Chaos, Null), else Harness (02 9.1).
	enum class EFrameDriver : uint8
	{
		Auto,
		Harness,
		Native,
	};

	enum class EPhysicsRunMode : uint8
	{
		Bench,
		Behavior,
		Viz,
	};

	// Mass rule axis (02 10, DL17): UEBodyUtils = M^RaiseMassToPower (UE), Physical = exponent 1.
	enum class EMassRule : uint8
	{
		UEBodyUtils,
		Physical,
	};

	struct FPhysicsTestSettings
	{
		EPhysicsTickMode TickMode = EPhysicsTickMode::FixedStep;
		float FixedDeltaTime = 1.0f / 60.0f;           // s
		ESubstepClampRule SubstepClampRule = ESubstepClampRule::UE5ChaosScene;
		EFrameDriver FrameDriver = EFrameDriver::Auto;
		EMassRule MassRule = EMassRule::UEBodyUtils;
		// Physics profile axis (02 10): false = Normalized "sleep off" (PhysX eDISABLE_SLEEPING, Jolt mAllowSleeping,
		// Bullet DISABLE_DEACTIVATION, Chaos p.Chaos.Solver.Sleep.Enabled=0); the start-asleep step is skipped (02 6).
		bool bAllowSleeping = true;
		// Restitution threshold in cm/s for every engine; <= 0 = each engine's UEParity mapping (02 10, DL35, Q11).
		float RestitutionThresholdVelocity = -1.0f;
		// Run options (not part of physics_sha256).
		EPhysicsRunMode RunMode = EPhysicsRunMode::Bench;
		uint32 InstrumentFlags = 0;
		int32 NumThreads = 1;
		uint64 AffinityMask = 0x1;
		int32 HashEvery = 0;        // 0 = final frame only (headline); > 0 only in non-headline runs
		int32 CountersEvery = 0;    // any PT_INSTR_COUNTERS run is non-headline
		int32 CaptureEvery = 0;
	};

	[[nodiscard]] const char* ToString(EPhysicsTickMode Value);
	[[nodiscard]] const char* ToString(ESubstepClampRule Value);
	[[nodiscard]] const char* ToString(EFrameDriver Value);
	[[nodiscard]] const char* ToString(EPhysicsRunMode Value);
	[[nodiscard]] const char* ToString(EMassRule Value);
	[[nodiscard]] const char* ToString(EChaosThreadingMode Value);
	[[nodiscard]] const char* ToString(EFrictionCombineMode Value);
	[[nodiscard]] const char* ToString(ESleepFamily Value);
}
