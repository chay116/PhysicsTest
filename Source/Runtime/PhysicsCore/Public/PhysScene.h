// PhysScene.h - FPhysScene, the only scene (02_Architecture 6, 9.1, A.9). [MVP M1 skeleton; M2 drives the C ABI; M3 run hashes
// and the debug snapshot read]
// Folds UWorld's physics part, FPhysScene_Chaos and the host side of FChaosScene into one object that owns its assets:
// materials, body setups, bodies and constraints live in std::deque storage, so pointers handed out stay valid.
//
// Life cycle (UE order, 02 6):
//   FPhysScene(Params)          registry -> backend module (InitGamePhys once) -> CreateScene; the harness physics console
//                               variables are set to the resolved config's values (see below)
//   CreatePhysicalMaterial / CreateBodySetup / SpawnBody(Template, Setup, TM)
//     -> FBodyInstance::InitBody -> UpdateMassProperties (harness BodyUtils) -> DeferAddActor
//   FlushDeferredActors()       [T_create] one batch in creation order: CreateMaterials + CreateActors (in the backend:
//                               CreateActor ... one AddActorsToScene ... InitDynamicProperties ... put-to-sleep for
//                               !bStartAwake unless sleeping is off), then one full state read
//   PrepareForSimulation()      [T_prepare]
//   Tick(DeltaSeconds)          SetUpForFrame (FStepPlanner) -> StartFrame (OnPhysScenePreTick(FrameDeltaTime), then the
//                               frame driver, below) -> WaitPhysScenes -> EndFrame ([T_pull] ReadBodyStates(ACTIVE_ONLY)
//                               -> FBodyInstance::State, then OnPhysScenePostTick)
//
// Frame drivers (02 9.1, DL08). EFrameDriver::Auto picks Native for a backend with PT_CAP_NATIVE_FRAME_MODEL (Chaos,
// Null) and Harness otherwise. Both broadcast OnPhysSceneStep(FrameDeltaTime) once per frame right after PreTick (UE5
// FPhysScene_Chaos::OnStartFrame), except under the Harness driver's UE4PhysSubstep rule, which calls it before every
// substep with the substep dt (UE4 FPhysSubstepTask [M]). Both push on every frame, also on one that runs no step (dt at
// or below MinPhysicsDeltaTime, an AsyncFixed frame that pushes nothing): what was issued during a frame reaches the
// backend in that frame, as UE5's game-thread writes do, so reads and later writes see it.
//   Native   [T_push] one command flush: the queue in issue order, then the frame's kinematic targets and forces;
//            [T_step] backend SetUpForFrame(the UE arguments, once) -> StartFrame -> WaitPhysScenes -> EndFrame, and
//            PtFrameResult::NumStepsTaken must equal FStepPlan::NumSteps (else Fatal). The scene is created with the
//            plan's AsyncDt. SimTime advances by FStepPlan::InternalStepDeltaTime per step (what the engine steps with).
//   Harness  a frame without steps: [T_push] the queue only. Per planned step: [T_push] the queue (what was issued since
//            the previous push), kinematic targets and forces by the plan's rule, [T_step] backend SetUpForFrame(&G,
//            StepDeltaTime, 0, 0, 0, 1, false) -> StartFrame -> WaitPhysScenes -> EndFrame (NumStepsTaken must be 1). The
//            scene is created with AsyncDt -1: the harness plans async steps itself. SimTime advances by
//            dt_engine = (double)StepDeltaTime per step.
//            UE5ChaosScene (Chaos parity): every force on every step of a frame that pushes steps, averaged over the
//              frames since the last push (AsyncFixed frames without steps carry them); kinematic targets Lerp / Slerp
//              by 1 / (N - k) from the pose of the previous step when the solver substeps, else reached on the first step
//            UE4PhysSubstep (PhysX UE4Compat): bAllowSubstepping forces on every substep from the one they were issued
//              in, the others on that substep only; kinematic targets with bAllowSubstepping Lerp / Slerp at (k + 1) / N
//              from the frame's start pose, the others are reached on the first substep
// Forces (both drivers): UE's AddForce / AddTorque wake a sleeping body on the game thread when they are called. The
// scene queues that wake as a WAKE_UP in issue order and sends the force itself with PT_CMDF_NO_WAKE at the push (Native:
// once per frame; Harness: per step), so a body put to sleep after the force stays asleep (docs/Abi.md 5.10). In
// FixedStep and Sync both drivers therefore send the same commands in the same order and agree bit for bit (A10).
// Frame state machine: Idle -> SetUp -> Started -> Waited -> Idle (the ABI's phases, 02 7.1). Calling a frame verb out of
// order, or creating / spawning / flushing / preparing outside Idle, is a checkf failure. OnPhysScenePreTick runs in
// SetUp, OnPhysSceneStep in Started, OnPhysScenePostTick in Idle. A backend error during a frame is Fatal.
//
// Harness physics console variables (p.ComNudgeAffectsInertia, p.SensitiveSleepThresholdMultiplier) are process-global,
// as in UE, but the resolved config records (and hashes) a value for each. The constructor therefore sets every one to
// the config's value (built-in default when the config has none or no config is given), forcing it over a stale
// higher-priority value with a warning; SpawnBody and FlushDeferredActors then verify that nothing changed them
// (Fatal otherwise). Scenes that live at the same time must agree on these values.
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
#include "PhysicsTestAbi/PtBackendAbi.h"

#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace PhysicsTest
{
	struct FResolvedPhysicsConfig;
	struct FBackendInfo;
	struct FPhysicsDebugSnapshot;
	class FPhysicsBackendScene;
	class FPhysScene;

	struct FPhysSceneInitParams
	{
		std::string DebugName;
		// A registry name (FPhysicsBackendRegistry::GetAvailableNames: "Null", "NullDll", ...). Empty: the config's
		// engine, else "Null".
		std::string BackendName;
		const FResolvedPhysicsConfig* Config = nullptr;   // nullptr = built-in defaults
		// PtSceneDesc capacity hints (0 = none; 02 9.4): a scenario fills them from its declared counts before the scene
		// (and with it the backend scene) is created. Engines with fixed capacities (Jolt PhysicsSystem::Init, M3) size
		// their pools from them; overflowing a hint is the backend's G12 failure, not a silent reallocation.
		uint32 MaxBodiesHint = 0;
		uint32 MaxBodyPairsHint = 0;
		uint32 MaxContactConstraintsHint = 0;
	};

	// UE delegate names (PhysScene_PhysX.h, PhysScene_Chaos.h); single-cast in the harness. PreTick and PostTick receive
	// FStepPlan::FrameDeltaTime (UE5 MDeltaTime: the frame dt after SetUpForFrame's clamp, not the raw Tick dt).
	using FOnPhysScenePreTick = std::function<void(FPhysScene*, float /*FrameDeltaTime*/)>;
	using FOnPhysSceneStep = std::function<void(FPhysScene*, float /*DeltaTime*/)>;   // once per frame (frame dt); UE4PhysSubstep: per substep
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
		// Template.CollisionFilter must be valid (ObjectType < NumCollisionChannels; the marshalling rejects it too).
		FBodyInstance* SpawnBody(const FBodyInstance& Template, const FBodySetup* Setup, const FTransform& Transform);
		// Data only until the M9 joints: stored and counted, never sent to a backend. Bodies must be this scene's.
		FConstraintInstance* SpawnConstraint(const FConstraintInstance& Template, FBodyInstance* Body1, FBodyInstance* Body2);
		// Only bodies spawned by this scene (SpawnBody) can be deferred, once: a body that is already deferred or already has
		// a backend actor is Fatal (one FBodyInstance never gets two actors; FBodyInstance::InitBody terminates a previous
		// registration first).
		void DeferAddActor(FBodyInstance* Body);
		// UE5 FPhysScene_Chaos::RemoveBodyInstanceFromPendingLists_AssumesLocked (PhysScene_Chaos.h:230 [V], minus SceneType):
		// drops a body that was deferred but not flushed yet, so the next FlushDeferredActors does not create an actor for it.
		// FBodyInstance::TermBody calls it. No effect for a body that is not pending.
		void RemoveBodyInstanceFromPendingLists_AssumesLocked(FBodyInstance* BodyInstance);
		// [T_create] All deferred bodies in creation order, as one batch (see above).
		void FlushDeferredActors();
		// Releases the body's backend actor (ReleaseActors; phase Idle). FBodyInstance::TermBody calls it.
		void ReleaseBody(FBodyInstance* Body);
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

		// ---- per-frame commands (UE5 FPhysScene_Chaos verbs, PhysScene_Chaos.h:232-238 [V]; see the frame drivers above;
		// released bodies drop theirs). UE5's scene verbs forward to FPhysicsInterface (PhysScene_Chaos.cpp:1670-1713 [V]);
		// the harness routes the other way - FPhysicsInterface hands forces, torques and kinematic targets to the owning
		// scene - so that every entry point follows the scene's frame rule.
		// Every force and torque: FBodyInstance::AddForce -> FPhysicsInterface::AddForce_AssumesLocked (as in UE5,
		// BodyInstance.cpp:3769-3779 [V]) -> here. A simulated body gets UE's game-thread wake now (WAKE_UP in issue
		// order); the force is sent at the push with PT_CMDF_NO_WAKE. Native: once per frame, the backend applies UE5's rule
		// (bAllowSubstepping is informational). Harness: the plan's rule above.
		void AddForce_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Force, bool bAllowSubstepping, bool bAccelChange);
		void AddTorque_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Torque, bool bAllowSubstepping, bool bAccelChange);
		// A kinematic body reaches TargetTM by the end of the frame's steps (the last target of a frame wins).
		// FBodyInstance::SetBodyTransform (bAllowSubstepping true, as UE5) and FPhysicsInterface::SetKinematicTarget_AssumesLocked
		// (false) end here. bAllowSubstepping matters only for the Harness driver's UE4PhysSubstep rule.
		void SetKinematicTarget_AssumesLocked(FBodyInstance* BodyInstance, const FTransform& TargetTM, bool bAllowSubstepping);
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
		[[nodiscard]] double GetSimTime() const;          // sum of the engine step dts (see the frame drivers above)
		[[nodiscard]] uint64 GetFrameCount() const;
		[[nodiscard]] uint64 GetStepCount() const;
		[[nodiscard]] const FPhysSceneStats& GetStats() const;
		[[nodiscard]] const FFrameTiming& GetLastFrameTiming() const;
		[[nodiscard]] const FStepPlan& GetLastPlan() const;
		[[nodiscard]] const std::string& GetDebugName() const;
		[[nodiscard]] const std::string& GetBackendName() const;
		// The driver in use (Auto resolved: Harness or Native).
		[[nodiscard]] EFrameDriver GetFrameDriver() const;
		[[nodiscard]] FPhysicsBackendScene& GetBackendScene() const;
		[[nodiscard]] const FBackendInfo& GetBackendInfo() const;
		// Untimed backend reads (Idle only).
		[[nodiscard]] uint64 ComputeNativeStateHash() const;
		[[nodiscard]] std::string DumpEffectiveSettings() const;
		// Debug snapshot (untimed, Idle; PROVISIONAL(M9) ABI): particles and, when the backend collects them (Jolt: viz runs
		// or PT_INSTR_CONTACTS), contact points in the Chaos convention. False when the backend has no snapshot
		// (PT_E_UNSUPPORTED, e.g. Null); a failing backend call is Fatal.
		bool CaptureDebugSnapshot(FPhysicsDebugSnapshot& Out, uint32 SnapshotFlags = PT_SNAP_PARTICLES | PT_SNAP_CONTACTS) const;

		// ---- run hashes (02 12.3, DL43) ----
		// desc_sha256: only engine-independent inputs - every material, shape, actor create info and convex vertex the
		// flushes sent (as marshalled, the same bytes for every backend), the constraints, the gravity and the dt sequence:
		// NominalDeltaTime plus the harness plan (steps and step dt) of each frame dt in FrameDeltaSeconds, planned with this
		// scene's tick mode from a fresh accumulator. Equal for every engine and variant of one scene and profile.
		[[nodiscard]] std::string ComputeDescSha256(std::span<const float> FrameDeltaSeconds) const;
		// options_sha256: the PtSceneDesc fields outside desc_sha256 (run mode, instrumentation, sim flags, async dt,
		// restitution threshold, velocity limit, iterations, threads, capacity hints, engine options in order) and the
		// frame driver. May differ per engine and variant.
		[[nodiscard]] std::string ComputeOptionsSha256() const;
		// The PtSceneDesc this scene was created with (pointers cleared) and its engine options, for manifests.
		[[nodiscard]] const PtSceneDesc& GetSceneDesc() const;
		[[nodiscard]] const std::vector<std::pair<std::string, std::string>>& GetSceneOptions() const;

	private:
		// Harness: FPhysicsInterface::SetGlobalPose_AssumesLocked of a kinematic body (the teleport itself is queued there).
		// UE5 makes the teleport pose the kinematic target with zero velocity (ChaosEngineInterface.cpp:2550-2557 [V]): the
		// body's pending target becomes the pose, and a later target of the frame starts from it (Harness interpolation).
		void TeleportKinematic_AssumesLocked(FBodyInstance* BodyInstance, const FTransform& Pose);
		// Harness: UE's game-thread dynamics dirty flag for the forces the scene holds (ParticleHandle.h:3755-3766 [V]).
		// FPhysicsInterface::PutToSleep_AssumesLocked marks the body's pending forces clean - the push drops them, as UE
		// never pushes them - and a later force, torque or impulse on the body marks them dirty again ("the old forces will
		// remain and the new ones will accumulate").
		void MarkDynamicsClean_AssumesLocked(const FBodyInstance* BodyInstance);
		void MarkDynamicsDirty_AssumesLocked(const FBodyInstance* BodyInstance);
		// These three hooks exist for FPhysicsInterface only (M2 review): a caller outside it could drop or restore held
		// forces, or rewrite a pending kinematic target, without the queued command that A10's two-driver invariant needs.
		friend class FPhysicsInterface;

		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};
}
