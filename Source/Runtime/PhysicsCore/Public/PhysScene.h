// PhysScene.h - FPhysScene, the only scene (02_Architecture 6, A.9). [MVP M1 skeleton; M2 drives the C ABI; M3 hashes]
// Folds UWorld's physics part, FPhysScene_Chaos and the host side of FChaosScene into one object that owns its assets:
// materials, body setups, bodies and constraints live in std::deque storage, so pointers handed out stay valid.
//
// Life cycle (UE order, 02 6):
//   CreatePhysicalMaterial / CreateBodySetup / SpawnBody(Template, Setup, TM)
//     -> FBodyInstance::InitBody -> UpdateMassProperties (harness BodyUtils) -> DeferAddActor
//   FlushDeferredActors()       one batch in creation order (in the backend: CreateActor ... one AddActorsToScene ...
//                               InitDynamicProperties ... put-to-sleep for !bStartAwake unless sleeping is off)
//   PrepareForSimulation()
//   Tick(DeltaSeconds)          SetUpForFrame (FStepPlanner) -> StartFrame (OnPhysScenePreTick with the clamped frame
//                               dt FStepPlan::FrameDeltaTime, then, Harness driver: OnPhysSceneStep + one engine step
//                               per planned step) -> WaitPhysScenes -> EndFrame -> OnPhysScenePostTick
// Frame state machine: Idle -> SetUp -> Started -> Waited -> Idle (the ABI's phases, 02 7.1). Calling a frame verb out
// of order, or creating / spawning / flushing / preparing outside Idle, is a checkf failure. OnPhysScenePreTick runs
// in SetUp (after planning, before the steps), OnPhysSceneStep in Started, OnPhysScenePostTick in Idle.
//
// M1 backend: a stub that assigns handles in creation order, sets each body's initial object state, and "steps" by
// counting - it does not move bodies. M2 replaces it with FPhysicsBackendScene over the C ABI without changing this
// interface.
#pragma once

#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Physics/PhysSceneStats.h"
#include "Physics/StepPlanner.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsSettings.h"

#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace PhysicsTest
{
	struct FResolvedPhysicsConfig;
	class FPhysScene;

	struct FPhysSceneInitParams
	{
		std::string DebugName;
		std::string BackendName = "Stub";                 // M1: only the stub exists
		const FResolvedPhysicsConfig* Config = nullptr;   // nullptr = built-in defaults
	};

	// UE delegate names (PhysScene_PhysX.h, PhysScene_Chaos.h); single-cast in the harness. PreTick and PostTick receive
	// FStepPlan::FrameDeltaTime (UE5 MDeltaTime: the frame dt after SetUpForFrame's clamp, not the raw Tick dt).
	using FOnPhysScenePreTick = std::function<void(FPhysScene*, float /*FrameDeltaTime*/)>;
	using FOnPhysSceneStep = std::function<void(FPhysScene*, float /*StepDeltaTime*/)>;   // Harness driver, per step
	using FOnPhysScenePostTick = std::function<void(FPhysScene*, float /*FrameDeltaTime*/)>;

	enum class EPhysSceneFramePhase : uint8
	{
		Idle,
		SetUp,
		Started,
		Waited,
	};

	class FPhysScene
	{
	public:
		explicit FPhysScene(const FPhysSceneInitParams& Params);
		~FPhysScene();
		FPhysScene(const FPhysScene&) = delete;
		FPhysScene& operator=(const FPhysScene&) = delete;

		// ---- assets (stable addresses) ----
		FPhysicalMaterial* CreatePhysicalMaterial(const FPhysicalMaterial& Template);
		FBodySetup* CreateBodySetup(FBodySetup&& Setup);
		// The material bodies fall back to (UE: GEngine->DefaultPhysMaterial): FPhysicalMaterial defaults with the
		// resolved [PhysicalMaterialDefaults].
		[[nodiscard]] const FPhysicalMaterial* GetDefaultPhysicalMaterial() const;

		// ---- bodies and constraints (~ the component owning FBodyInstance + FInitBodiesHelper) ----
		// Copies Template, then InitBody(Setup, Transform, this): mass properties now, the actor at the next flush.
		// Template.CollisionFilter must be valid (ObjectType < NumCollisionChannels).
		FBodyInstance* SpawnBody(const FBodyInstance& Template, const FBodySetup* Setup, const FTransform& Transform);
		// Data only until the M9 joints: stored and counted, never sent to a backend.
		FConstraintInstance* SpawnConstraint(const FConstraintInstance& Template, FBodyInstance* Body1, FBodyInstance* Body2);
		void DeferAddActor(FBodyInstance* Body);
		// [T_create] All deferred bodies in creation order, as one batch.
		void FlushDeferredActors();
		// [T_prepare] The same untimed opportunity for every engine (Jolt OptimizeBroadPhase, ...).
		void PrepareForSimulation();

		// ---- frame (~ UWorld physics tick + FChaosScene) ----
		void Tick(float DeltaSeconds);
		void SetUpForFrame(const FVector* NewGrav, float InDeltaSeconds, float InMinPhysicsDeltaTime, float InMaxPhysicsDeltaTime,
			float InMaxSubstepDeltaTime, int32 InMaxSubsteps, bool bSubstepping);
		void StartFrame();
		void WaitPhysScenes();
		void EndFrame();
		[[nodiscard]] bool IsSimulating() const;
		[[nodiscard]] EPhysSceneFramePhase GetFramePhase() const;

		FOnPhysScenePreTick OnPhysScenePreTick;
		FOnPhysSceneStep OnPhysSceneStep;
		FOnPhysScenePostTick OnPhysScenePostTick;

		// ---- queries ----
		[[nodiscard]] std::span<FBodyInstance* const> GetBodies() const;
		[[nodiscard]] std::span<FConstraintInstance* const> GetConstraints() const;
		[[nodiscard]] int32 GetNumDeferredActors() const;
		[[nodiscard]] const FPhysicsSettings& GetSettings() const;
		[[nodiscard]] const FPhysicsTestSettings& GetTestSettings() const;
		[[nodiscard]] FVector GetGravity() const;
		[[nodiscard]] double GetSimTime() const;          // sum of the engine step dts (each as dt_engine)
		[[nodiscard]] uint64 GetFrameCount() const;
		[[nodiscard]] uint64 GetStepCount() const;
		[[nodiscard]] const FPhysSceneStats& GetStats() const;
		[[nodiscard]] const FFrameTiming& GetLastFrameTiming() const;
		[[nodiscard]] const FStepPlan& GetLastPlan() const;
		[[nodiscard]] const std::string& GetDebugName() const;
		[[nodiscard]] const std::string& GetBackendName() const;

	private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};
}
