// PhysScene_Jolt.h - FPhysScene_Jolt and FJoltBackend: Jolt 5.6.0 behind the C ABI (02_Architecture 8.3, A.15).
// [MVP M3]   PT_BACKEND_NS=Jolt
//
// Units: Jolt is tuned for SI metres (cm input runs into the 500 m/s clamp and absurd masses, 01_Feasibility 3.3 [V]), so
// every length is converted with BackendCommon's GMetres (x0.01; inertia, torque and angular impulse x1e-4) and nowhere
// else. Mass properties come from the harness (D5): MassPropertiesOverride + EOverrideMassProperties::
// MassAndInertiaProvided with I = R diag(I) R^T in body space (R = the harness principal frame).
//
// Shapes (DL42, PtShapeWrapping.h): one element whose element frame and COM frame are the actor frame -> BoxShape /
// SphereShape directly; a capsule needs RotatedTranslatedShape for UE Z -> Jolt Y (axis_remap); otherwise
// RotatedTranslatedShape (one placed element) or StaticCompoundShape (several), and OffsetCenterOfMassShape when Jolt's
// geometric COM differs from the harness COM. Every wrapper is counted into shape.wrapping (M3.BoxPile: 0).
//
// Body creation follows UE's FInitBodiesHelper order (02 6, DL38; BodyInstance.cpp:1669, 1695-1706, 4648-4743 [V]):
//   1 per body   BodyCreationSettings with the creation-time flags (motion type, gravity factor, CCD motion quality,
//                gyroscopic force, max linear velocity, iteration overrides) and the harness mass properties
//   2 per body   shape, collision group (UE channels), material (friction / restitution + combine modes in the user data)
//   3 once       BodyInterface::AddBodiesPrepare + AddBodiesFinalize for the whole batch (EActivation::Activate)
//   4 per body   InitDynamicProperties in UE order: damping, max angular velocity, (max depenetration: no per-body rate in
//                Jolt, recorded), initial velocity, (sleep / stabilisation multipliers: no per-body threshold, recorded)
//   5 per body   !PT_BODY_START_AWAKE and dynamic: BodyInterface::DeactivateBody, skipped (and counted) when the scene
//                disables sleeping
// Jolt defaults that would bias a comparison are overridden: damping 0.05 (BodyCreationSettings.h:109-110 [V]) gets the
// UE value, the 500 m/s linear clamp (:111) is lifted, the angular clamp is the profile's (UEParity 62.83 rad/s), sleep is
// off per body in Normalized (mAllowSleeping, :106 [V], DL48).
//
// Frames (Harness driver, no NATIVE_FRAME_MODEL): SetUpForFrame stores dt and sets the gravity only when it changes
// (DL32); StartFrame applies the pending kinematic targets (MoveKinematic) and zeroes the velocity of a kinematic body
// that got none this step (Chaos mode Reset), then the pending impulses and forces (below), then PhysicsSystem::Update(dt,
// 1, temp allocator, JobSystemThreadPool(T-1)) and records EPhysicsUpdateError; EndFrame reports one step.
// ReadBodyStates(ACTIVE_ONLY) walks GetActiveBodiesUnsafe plus the bodies that left the active list or changed by a
// command since the previous read.
//
// Accumulating commands are deferred to the step, never applied at push (docs/Abi.md 5.10, 7.1): ADD_IMPULSE /
// ADD_VELOCITY / ADD_ANGULAR_* add a velocity change to the body record (the angular one through the world inverse
// inertia at push time, as Null), and StartFrame adds it to the engine velocity right before Update, as Chaos integrates
// LinearImpulseVelocity (PBDRigidsEvolutionGBF.cpp:920-924 [V]). Jolt's Body::AddImpulse would change the velocity at
// once (Body.inl:133-154 [V]): a later SET_LINEAR_VELOCITY in the same batch would drop the impulse and a read before the
// step would already see it. A body that is asleep at the step keeps its impulse pending (PUT_TO_SLEEP after the impulse,
// as Null). ADD_FORCE / ADD_TORQUE are buffered the same way and handed to Body::AddForce / AddTorque at the step only for
// a body that is awake then; the buffer is cleared every step, so a force never survives a PUT_TO_SLEEP into a later wake
// (Jolt resets mForce only for active bodies, PhysicsSystem.cpp:2541 [V]).
//
// Debug snapshot (contact listener in viz runs / PT_INSTR_CONTACTS only, PROVISIONAL(M9)): particles with Jolt's world bounds, and the contacts of the last step from
// FJoltContactListener in the Chaos convention: Normal = -mWorldSpaceNormal, Phi = -(p1 - p2) . n per point (M4; M3 gave
// every point the manifold's mPenetrationDepth), then Body0 = the more mobile body (CanonicalizeContact). Impulses are NaN
// (Q8: no local patch), "active" is PT_CONTACT_ACTIVE_APPROX for Phi <= 0. The contacts of sleeping islands are kept from
// the step they fell asleep in and reported with their last flags plus PT_CONTACT_SLEEPING (M4, UpdateSleepingContacts; Chaos
// keeps a sleeping constraint's last solver results, so a resting sleeping contact still draws active). A body put to sleep by
// PT_CMD_PUT_TO_SLEEP between steps keeps its pairs from the last step; a kept pair goes when a body of it wakes or a command
// sets its pose (SET_GLOBAL_POSE, SET_BODY_STATE: the kept points would be stale), and comes back only when Jolt reports the
// pair again (FlushKeptContactCommands, M4 fix round 2). Native islands are not offered (GetIslandIndexInternal is internal);
// the viewer builds its own (FIslandBuilder, M4).
// PtContactPoint::Shape0 / Shape1 are the harness element indices of the manifold's sub shapes (M4). Known limit (docs/Abi.md
// 7.1, M9): a body's friction / restitution are its first element's material (Jolt keeps one per body).
//
// ODR / flag isolation (docs/Abi.md 5.13, CTest PhysicsTest.Probe.AdapterComdats): these TUs are compiled with Jolt's
// /arch:AVX2, so every container of the adapter is a JPH::Array (a Jolt type) and never a std:: container. A
// std::vector<uint32> here would be a COMDAT the linker may pick for the whole exe, so harness code would run the AVX2
// copy.
#pragma once

#include "JoltContactListener.h"
#include "JoltIncludes.h"
#include "JoltLayers.h"
#include "JoltTempAllocator.h"
#include "PhysicsTestAbi/PtBackendKit.h"
#include "PtShapeWrapping.h"
#include "PtTypes.h"

#include <memory>

namespace PhysicsTest::JoltBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	// Scene options ([PhysicsTest.Backend.Jolt] keys of a backend variant, case-insensitive; lengths in cm).
	struct FJoltSceneOptions
	{
		uint32 NumVelocitySteps = 10;            // PhysicsSettings::mNumVelocitySteps (Jolt default)
		uint32 NumPositionSteps = 2;             // PhysicsSettings::mNumPositionSteps
		double ConvexRadiusCm = 5.0;             // BoxShape convex radius, cDefaultConvexRadius 0.05 m (PhysicsSettings.h:16 [V])
		double PenetrationSlopCm = 2.0;          // mPenetrationSlop 0.02 m (PhysicsSettings.h:53 [V])
		double SpeculativeContactDistanceCm = 2.0;   // mSpeculativeContactDistance 0.02 m
		double Baumgarte = 0.2;                  // mBaumgarte
		bool bUseLargeIslandSplitter = true;     // mUseLargeIslandSplitter
		bool bDeterministicSimulation = true;    // mDeterministicSimulation
		uint64 TempAllocatorBytes = 0;           // 0 = sized from the capacity hints
	};

	// One [PhysicsTest.Backend.Jolt] key (CreateScene options, SetOption). PT_OK, or PT_E_INVALID_ARG with a message in
	// Error (an unknown key or a malformed value).
	PtResult ApplySceneOption(FJoltSceneOptions& Options, ::PhysicsTest::Abi::FStringRef Key, ::PhysicsTest::Abi::FStringRef Value, char (&Error)[256]);

	struct FJoltBody
	{
		JPH::BodyID Id;
		uint32 Motion = PT_MOTION_STATIC;
		uint32 Flags = 0;
		uint32 MaterialIndex = 0;
		double Mass = 0.0;                       // harness values (ReadMassProperties of static / kinematic bodies)
		PtVec3 InertiaDiagonal = {};
		PtTransform CenterOfMassLocal = {};
		PtTransform KinematicTarget = {};        // cm, actor pose
		PtVec3 KinematicOriginVelocity = {};     // cm/s: (target origin - origin) / dt of the last MoveKinematic (Chaos reports
		                                         // the origin's velocity, PtBodyState)
		// Deferred to the next step (StartFrame), Jolt units: velocity changes of ADD_IMPULSE / ADD_VELOCITY / ADD_ANGULAR_*
		// (m/s, rad/s) and the force / torque of ADD_FORCE / ADD_TORQUE (N, N*m; ACCEL_CHANGE already applied).
		JPH::Float3 PendingLinearVelocity{ 0.0f, 0.0f, 0.0f };
		JPH::Float3 PendingAngularVelocity{ 0.0f, 0.0f, 0.0f };
		JPH::Float3 PendingForce{ 0.0f, 0.0f, 0.0f };
		JPH::Float3 PendingTorque{ 0.0f, 0.0f, 0.0f };
		uint32 LastReadStamp = 0;                // dedupes one PT_READ_ACTIVE_ONLY report
		bool bReleased = false;
		bool bDirty = true;                      // changed by a command since the previous read
		bool bHasKinematicTarget = false;
		bool bKinematicMovedLastStep = false;
		bool bPendingImpulse = false;            // listed in FPhysScene_Jolt::PendingImpulseBodies
		bool bPendingForce = false;              // listed in FPhysScene_Jolt::PendingForceBodies
		// With the listener (viz runs or PT_INSTR_CONTACTS): a command since the last FlushKeptContactCommands that changes this
		// body's kept sleeping contacts (listed once in FPhysScene_Jolt::KeptContactBodies).
		bool bKeptContactsListed = false;
		bool bPutToSleepByCommand = false;       // PT_CMD_PUT_TO_SLEEP deactivated it: keep its last-step contacts
		bool bPoseSetByCommand = false;          // SET_GLOBAL_POSE / SET_BODY_STATE: its kept points are stale
	};

	class FPhysScene_Jolt final : public ::PhysicsTest::Abi::IPhysicsBackendScene
	{
	public:
		FPhysScene_Jolt(const PtSceneDesc& Desc, const FJoltSceneOptions& Options);
		~FPhysScene_Jolt() override;

		PtResult CreateMaterials(::PhysicsTest::Abi::TStridedView<PtPhysicalMaterial> Materials) override;
		PtResult CreateActors(const ::PhysicsTest::Abi::FActorBatchView& Batch) override;
		PtResult ReleaseActors(const uint32_t* BodyIndices, uint32_t Num) override;
		PtResult PrepareForSimulation(::PhysicsTest::Abi::FTextWriter& OutActions) override;
		PtResult SetUpForFrame(const ::PhysicsTest::Abi::FFrameParams& Params) override;
		PtResult StartFrame() override;
		PtResult WaitPhysScenes() override;
		PtResult EndFrame(PtFrameResult& Out) override;
		PtResult ValidateBodyCommand(const PtBodyCommand& Cmd) override;
		PtResult ApplyBodyCommand(const PtBodyCommand& Cmd) override;
		PtResult ReadBodyStates(uint32_t ReadFlags, uint32_t* OutIndices, PtBodyState* Out, uint32_t Capacity, uint32_t& OutCount) override;
		PtResult ReadMassProperties(uint32_t First, uint32_t Num, PtMassProperties* Out) override;
		PtResult GetStepStats(PtStepStats& Out) override;
		PtResult CaptureDebugSnapshot(const PtDebugSnapshotRequest& Req, PtDebugSnapshotCounts& Out) override;
		PtResult ReadDebugSnapshot(const PtDebugSnapshotBuffers& Buffers) override;
		PtResult ComputeNativeStateHash(uint64_t& OutHash) override;
		PtResult SetOption(::PhysicsTest::Abi::FStringRef Key, ::PhysicsTest::Abi::FStringRef Value) override;
		PtResult DumpEffectiveSettings(::PhysicsTest::Abi::FTextWriter& Out) override;
		PtResult GetMemoryStats(PtMemoryStats& Out) override;

	private:
		JPH::RefConst<JPH::Shape> MakeBodyShape(const PtActorCreateInfo& Actor, const ::PhysicsTest::Abi::TStridedView<PtShapeElem>& Shapes);
		JPH::RefConst<JPH::Shape> MakeElementShape(const PtShapeElem& Element);
		void EnsureTempAllocator();
		void ApplyPhysicsSettings();
		// StartFrame, right before Update: the deferred velocity changes and forces of the awake bodies.
		void ApplyPendingDynamics();
		// Every live body's BoxShape convex radius as the shapes hold it (shape.box_margin readback), through decorated and
		// compound shapes. false when there is no box.
		bool ReadBoxConvexRadii(double& OutMinCm, double& OutMaxCm) const;
		// With the listener (viz runs or PT_INSTR_CONTACTS), after each Update: keeps the contacts of bodies that are asleep
		// now. Jolt runs no collision detection for sleeping bodies, so the listener reports nothing for them; Chaos keeps a
		// sleeping island's constraints, and the snapshot reports these as PT_CONTACT_SLEEPING (M4, islands and DrawCollisions).
		void UpdateSleepingContacts();
		// With the listener, StartFrame before it is cleared: what commands did to the kept contacts since the last step.
		// A body put to sleep between steps (PT_CMD_PUT_TO_SLEEP) keeps its pairs from the last step's listener contents, as
		// if it had fallen asleep in that step; the kept pairs of a body whose pose a command set (SET_GLOBAL_POSE,
		// SET_BODY_STATE - a static or DontActivate pose leaves a sleeping pair asleep) are dropped, because their world
		// points no longer match; a body a command woke loses its kept pairs. One pass over the listener and the kept list.
		void FlushKeptContactCommands();
		void NoteKeptContactCommand(uint32 BodyIndex, bool bPutToSleep, bool bPoseSet);
		// A pair with a dynamic body whose dynamic bodies are all asleep (static and kinematic ends never sleep).
		bool IsSleepingPair(const FJoltRawContact& Contact) const;
		// A Jolt assert that fired since the last check - inside Update or not (creation, commands, reads, snapshots) - sets
		// GJoltErrorAssert in this frame's and the scene's error bits (M3 review: asserts outside Update were only counted).
		void CheckJoltAsserts();
		void CheckJoltAssertsBetweenFrames();
		PtBodyState MakeState(const FJoltBody& Body) const;
		JPH::Body* GetBody(const FJoltBody& Body) const;
		void MarkDirty(uint32 BodyIndex);

		PtSceneDesc Desc;
		FJoltSceneOptions Options;
		FJoltBroadPhaseLayerInterface BroadPhaseLayers;
		FJoltObjectVsBroadPhaseLayerFilter ObjectVsBroadPhaseFilter;
		FJoltObjectLayerPairFilter ObjectLayerPairFilter;
		JPH::Ref<FJoltUEGroupFilter> GroupFilter;
		std::unique_ptr<JPH::PhysicsSystem> System;
		std::unique_ptr<JPH::JobSystemThreadPool> JobSystem;
		std::unique_ptr<FJoltTempAllocator> TempAllocator;
		std::unique_ptr<FJoltContactListener> ContactListener;   // viz runs / PT_INSTR_CONTACTS only

		// JPH::Array only (see the ODR note at the top of this file).
		JPH::Array<PtPhysicalMaterial> Materials;
		JPH::Array<FJoltBody> Bodies;
		JPH::Array<uint8> MobilityRanks;         // per body, for CanonicalizeContact
		JPH::Array<uint32> Kinematics;           // indices of kinematic bodies (targets and Reset each step)
		JPH::Array<uint32> DirtyBodies;          // changed by a command since the previous ACTIVE_ONLY read
		JPH::Array<uint32> PendingImpulseBodies; // bodies with a deferred velocity change (reserved to MaxBodies: no allocation at push)
		JPH::Array<uint32> PendingForceBodies;   // bodies with a deferred force / torque (reserved to MaxBodies)
		JPH::Array<uint32> PrevActive;           // the active bodies the previous ACTIVE_ONLY read reported
		JPH::Array<uint32> ReadScratch;          // the report list of one ACTIVE_ONLY read (reserved: no allocation per read)
		JPH::Array<uint32> ActiveScratch;
		JPH::Array<PtParticleSnapshot> SnapshotParticles;
		JPH::Array<PtContactPoint> SnapshotContacts;
		JPH::Array<FJoltRawContact> SleepingContacts;   // see UpdateSleepingContacts (reserved to the listener capacity)
		JPH::Array<FJoltRawContact> SleepingScratch;
		JPH::Array<uint32> KeptContactBodies;           // see FJoltBody::bKeptContactsListed (reserved to MaxBodies with the listener)
		bool bSleepingContactsDropped = false;
		uint32 AssertsSeen = 0;                         // GetNumJoltAsserts() at the last CheckJoltAsserts

		::PhysicsTest::Abi::FShapeWrappingCounts Wrapping;
		uint32 MaxBodies = 0;
		uint32 MaxBodyPairs = 0;
		uint32 MaxContactConstraints = 0;
		uint32 NumWorkerThreads = 0;
		uint32 ReadStamp = 0;
		uint32 NumStartAsleepSkipped = 0;
		uint32 NumIterationOverrides = 0;
		uint32 FrameErrorBits = 0;
		uint32 PendingErrorBits = 0;                    // seen between frames, reported by the next frame
		uint32 TotalErrorBits = 0;
		uint64 TotalSteps = 0;
		float StepDeltaTime = 0.0f;
		PtVec3 Gravity = {};
		bool bStepped = false;
		bool bSleepingAllowed = true;
	};

	class FJoltBackend final : public ::PhysicsTest::Abi::IPhysicsBackend
	{
	public:
		static constexpr const char* ModuleName = "Jolt";

		void GetInfo(PtBackendInfo& Out) const override;
		PtResult InitGamePhys(const PtInitParams& Params) override;
		PtResult TermGamePhys() override;
		PtResult CreateScene(const PtSceneDesc& Desc, ::PhysicsTest::Abi::IPhysicsBackendScene*& OutScene) override;
		void DestroyScene(::PhysicsTest::Abi::IPhysicsBackendScene* Scene) override;
		PtResult SetModuleOption(::PhysicsTest::Abi::FStringRef Key, ::PhysicsTest::Abi::FStringRef Value) override;
		PtResult DumpModuleSettings(::PhysicsTest::Abi::FTextWriter& Out) override;
		PtResult GetMemoryStats(PtMemoryStats& Out) override;

	private:
		uint32 NumLiveScenes = 0;
		uint32 MaxThreads = 1;
		bool bJoltRegistered = false;
	};

	// Engine error bits of PtFrameResult / PtStepStats: Jolt's EPhysicsUpdateError values as they are (ManifoldCacheFull
	// 1, BodyPairCacheFull 2, ContactConstraintsFull 4) plus the harness's own.
	inline constexpr uint32 GJoltErrorTempAllocatorFallback = 1u << 8;   // the temp block overflowed (heap fallback)
	inline constexpr uint32 GJoltErrorAssert = 1u << 9;                  // a Jolt assert fired (dbg builds)
	// The bits that mean "a capacity was exceeded" (bench exit code 5, G12): ManifoldCacheFull | BodyPairCacheFull |
	// ContactConstraintsFull | temp fallback. Published as the effective key errors.capacity_bits (docs/Abi.md 5.14).
	inline constexpr uint32 GJoltCapacityErrorBits = static_cast<uint32>(JPH::EPhysicsUpdateError::ManifoldCacheFull)
		| static_cast<uint32>(JPH::EPhysicsUpdateError::BodyPairCacheFull) | static_cast<uint32>(JPH::EPhysicsUpdateError::ContactConstraintsFull)
		| GJoltErrorTempAllocatorFallback;

	// The number of Jolt asserts this module has seen (JoltBackend.cpp; always 0 without JPH_ENABLE_ASSERTS).
	uint32 GetNumJoltAsserts();
	// Harness test hook (runtime scene option GJoltTestRaiseAssertKey, M4): counts and logs one assert exactly as the dbg assert
	// hook does, in every build, so the propagation of an assert outside PhysicsSystem::Update into the error bits is a unit
	// test. CreateScene rejects the key (ApplySceneOption), so no profile or variant file can reach it.
	inline constexpr const char* GJoltTestRaiseAssertKey = "pt.test.raise_assert";
	void RaiseTestAssert(const char* Where);
}
