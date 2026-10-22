// PhysScene_PhysX.h - FPhysScene_PhysX and FPhysXBackend: PhysX 5.11 behind the C ABI with UE4's integration shape,
// re-implemented clean room from the design text (02_Architecture 8.2, A.14; 03_WorkPlan M5). [MVP M5]   PT_BACKEND_NS=PhysX
//
// InitPhysScene builds the PxSceneDesc from FPhysXSceneConfig (the variant's [PhysicsTest.Backend.PhysX] keys):
// broad phase, solver, PCM / stabilisation / CCD / enhanced determinism / active actors flags, kinematic pair filtering,
// bounce threshold, dynamic tree rebuild rate, the UE4 filter shader, the CPU dispatcher of the thread budget, the
// FPhysXSimEventCallback (installed in viz runs or with PT_INSTR_CONTACTS), and PxSceneLimits from the capacity hints.
// Normalized's sleep off is PxSceneFlag::eDISABLE_SLEEPING (no zero-threshold workaround). The scene's LengthScale must match the process
// PxTolerancesScale (length = 100 cm in native units): a SdkDefault scene (m) under a UE4Compat PxPhysics (cm) is refused.
//
// CreateActors follows UE's FInitBodiesHelper order (02 6, A.14; BodyInstance.cpp:1669, 1695-1706, 4648-4743 [V]):
//   1 per body   CreateActor_AssumesLocked: createRigidStatic / createRigidDynamic with the creation-time flags
//                (eKINEMATIC, eDISABLE_GRAVITY, eENABLE_CCD, eENABLE_GYROSCOPIC_FORCES, max linear velocity)
//   2 per body   CreateShapesFromAggGeom (UE's FBodySetupShapeIterator): one exclusive PxShape per element
//                (bEnableShapeSharing = false), filter words, material, contact offset (UE4: clamp(0.02 x MinExtent, 2, 8)
//                cm [M formula, V constants]; MinExtent = radius or smallest half extent), rest offset from the element
//   3 once       PxScene::addActors for the whole batch (UE4 AddActorsToScene_AssumesLocked)
//   4 per body   InitDynamicProperties_AssumesLocked in UE order: mass / inertia / COM -> damping -> max angular velocity
//                -> max depenetration velocity -> initial velocity -> sleep threshold x multiplier -> stabilisation
//                threshold x multiplier -> solver iterations
//   5 per body   !PT_BODY_START_AWAKE (dynamic): setWakeCounter(0) + putToSleep - only possible for a body in a scene
//                (NpRigidDynamic.cpp:527 [V]); skipped (and counted) in an eDISABLE_SLEEPING scene, where PhysX would
//                reject putToSleep (NpRigidDynamic.cpp:521-525 [V])
//
// Frames (Harness driver, no NATIVE_FRAME_MODEL): SetUpForFrame stores dt and sets the gravity only when it changes
// (DL32); StartFrame applies the kinematic targets (setKinematicTarget; a body that moved last step and got none stops,
// Chaos EKinematicTargetMode::Reset), then the deferred impulses and forces (docs/Abi.md 7.1), then
// simulate(dt, nullptr, scratch, ScratchMemorySize) - UE4's TG_StartPhysics; WaitPhysScenes is fetchResults(true) - UE4's
// TG_EndPhysics; EndFrame reports one step and the PhysX error bits. ReadBodyStates(ACTIVE_ONLY) walks getActiveActors
// (eENABLE_ACTIVE_ACTORS | eEXCLUDE_KINEMATICS_FROM_ACTIVE_ACTORS, PxSceneDesc.h:92, :212 [V]) plus the kinematic
// bodies that moved, the bodies of the previous report and the bodies a command changed; without active actors
// (SdkDefault) it visits every body.
//
// Accumulating commands are deferred to the step as on every SDK backend (docs/Abi.md 5.10, 7.1): PhysX's
// addForce(eIMPULSE / eVELOCITY_CHANGE) and setLinearVelocity act on the body at once, so ADD_IMPULSE / ADD_VELOCITY /
// ADD_ANGULAR_* become a velocity change kept on the body record (the angular one through the world inverse inertia at
// push time) and added to the PhysX velocity right before simulate; a body asleep then keeps it. ADD_FORCE / ADD_TORQUE
// are handed to addForce / addTorque(eFORCE) at the step for an awake body only, and the list is cleared every step.
// ABI v1.0 (M6, docs/Abi.md 5.10): every velocity or state write wakes a sleeping body (UE 5.8.3's SetV / SetW); PhysX
// itself wakes for a non-zero vector even with autowake false (NpRigidDynamic.cpp:253, :286, :605-622 [V]) and the adapter
// wakes explicitly for a zero one.
//
// Debug snapshot (contact listener in viz runs / PT_INSTR_CONTACTS only): the last step's FPhysXSimEventCallback points in
// the Chaos convention (PhysX's normal already points from the second shape to the first, i.e. lies on Body1 toward
// Body0; Phi = separation; M6 contract (c): PointOn1 = the point of the report's second shape on the normal line through
// PxContactPairPoint::position (PhysXSimEventCallback.h), PointOn0 = PointOn1 + separation x normal), then Body0 = the more
// mobile body (CanonicalizeContact). PT_CONTACT_ACTIVE is exact: the point carried a normal impulse in its last solved
// step (PxContactPairPoint::impulse, as Chaos' bIsActive); PT_CONTACT_SPECULATIVE marks a separated point without one.
// The impulse values themselves stay NaN for now (Q8; the shared snapshot tests expect NaN, M9). PhysX reports no contact
// of a pair whose dynamic bodies all sleep - already in the step the island falls asleep in - so, as the Jolt backend does
// (Progress M4), the pairs of a sleeping island are kept (from the last step that reported them) and reported with their
// last flags plus PT_CONTACT_SLEEPING whenever the listener exists, with the same command rules (FlushKeptContactCommands).
// Each report of the last step goes to exactly one place at the next StartFrame: the kept pairs (a body put to sleep by
// command) or PrevStepContacts (the candidates for a pair that falls asleep in the coming step); M5 round 0 fixed the
// double listing of a commanded sleep.
//
// Joints (M9, docs/Abi.md 9.4; PhysXConstraint.h): CreateConstraints creates one PhysX joint per ABI joint with the variant's
// recipe (UE4Compat family: PxD6Joint with the actors swapped [M]; SdkDefault: PxSphericalJoint / PxRevoluteJoint), all or
// nothing; ReleaseActors releases the joints of a released body first. The debug snapshot reports every live joint
// (PT_CAP_JOINT_SNAPSHOT): the two world frames from the engine's actor poses and joint local poses, and the impulse on
// Body1 from PxConstraint::getForce x dt. Native debug draw (PT_CAP_NATIVE_DEBUG_DRAW; PhysXDebugRender.h): viz runs set the
// variant's PxVisualizationParameters at InitPhysScene, and PT_SNAP_NATIVE_DRAW turns PxScene::getRenderBuffer into
// PtDebugDrawCmd records; every other run leaves PhysX's visualization off and reports no command.
//
// Containers are physx::PxArray (PhysX's allocator, no std:: container in adapter state, docs/Abi.md 5.13).
#pragma once

#include "PhysicsFiltering.h"
#include "PhysXConstraint.h"
#include "PhysXCPUDispatcher.h"
#include "PhysXDebugRender.h"
#include "PhysXSceneConfig.h"
#include "PhysXSimEventCallback.h"
#include "PhysXSupport.h"
#include "PhysicsTestAbi/PtBackendKit.h"
#include "PtShapeWrapping.h"
#include "PtTypes.h"
#include "PtUnitScale.h"

#include <memory>

namespace PhysicsTest::PhysXBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	// Engine error bits of PtFrameResult / PtStepStats: the PxErrorCode values of new PhysX reports (GPhysXErrorBitsMask,
	// PhysXSupport.h) plus the harness's own.
	inline constexpr uint32 GPhysXErrorSimulateRefused = 1u << 8;   // PxScene::simulate / fetchResults returned false

	struct FPhysXBody
	{
		physx::PxRigidActor* Actor = nullptr;       // PxRigidStatic or PxRigidDynamic
		physx::PxRigidDynamic* Dynamic = nullptr;   // dynamic and kinematic bodies
		uint32 Motion = PT_MOTION_STATIC;
		uint32 Flags = 0;
		double Mass = 0.0;                          // harness values (ReadMassProperties of static bodies)
		PtVec3 InertiaDiagonal = {};
		PtTransform CenterOfMassLocal = {};
		PtTransform KinematicTarget = {};           // cm, actor pose
		PtVec3 KinematicOriginVelocity = {};        // cm/s and rad/s of the last target step: the origin's displacement / dt
		PtVec3 KinematicAngularVelocity = {};       // (Chaos reports the actor origin's velocity, PtBodyState)
		// Deferred to the next step (StartFrame), native units: velocity changes of ADD_IMPULSE / ADD_VELOCITY / ADD_ANGULAR_*
		// and the force / torque of ADD_FORCE / ADD_TORQUE (ACCEL_CHANGE already applied).
		physx::PxVec3 PendingLinearVelocity{ 0.0f };
		physx::PxVec3 PendingAngularVelocity{ 0.0f };
		physx::PxVec3 PendingForce{ 0.0f };
		physx::PxVec3 PendingTorque{ 0.0f };
		uint32 LastReadStamp = 0;                   // dedupes one PT_READ_ACTIVE_ONLY report
		bool bReleased = false;
		bool bDirty = true;                         // changed by a command since the previous read
		bool bHasKinematicTarget = false;
		bool bKinematicMovedLastStep = false;
		bool bPendingImpulse = false;
		bool bPendingForce = false;
		bool bKeptContactsListed = false;           // listed once in KeptContactBodies (listener present: viz or PT_INSTR_CONTACTS)
		bool bPutToSleepByCommand = false;
		bool bPoseSetByCommand = false;

		bool IsSimulatedDynamic() const { return Motion == PT_MOTION_DYNAMIC; }
	};

	class FPhysScene_PhysX final : public ::PhysicsTest::Abi::IPhysicsBackendScene
	{
	public:
		FPhysScene_PhysX(const PtSceneDesc& Desc, const FPhysXSceneConfig& Config);
		~FPhysScene_PhysX() override;

		// UE4 FPhysScene_PhysX::InitPhysScene: the PxScene from the variant config. PT_OK or an error with a message.
		PtResult InitPhysScene(char (&Error)[256]);

		PtResult CreateMaterials(::PhysicsTest::Abi::TStridedView<PtPhysicalMaterial> Materials) override;
		PtResult CreateActors(const ::PhysicsTest::Abi::FActorBatchView& Batch) override;
		PtResult ReleaseActors(const uint32_t* BodyIndices, uint32_t Num) override;
		PtResult CreateConstraints(::PhysicsTest::Abi::TStridedView<PtConstraintCreateInfo> Infos) override;   // M9, PhysXConstraint.cpp
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
		// 1 / 2 / 4 of the creation order (see the file comment).
		physx::PxRigidActor* CreateActor_AssumesLocked(const PtActorCreateInfo& Info);
		bool CreateShapesFromAggGeom(const PtActorCreateInfo& Info, const ::PhysicsTest::Abi::TStridedView<PtShapeElem>& Shapes, physx::PxRigidActor& Actor);
		void InitDynamicProperties_AssumesLocked(physx::PxRigidDynamic& Actor, const PtActorCreateInfo& Info);
		// UE4: clamp(ContactOffsetMultiplier x MinExtent, MinContactOffset, MaxContactOffset), cm.
		double ComputeContactOffsetCm(const PtShapeElem& Element) const;
		// shape.wrapping from the engine's actors (getNbShapes, the shapes' local poses and geometry, getCMassLocalPose).
		::PhysicsTest::Abi::FShapeWrappingCounts ReadWrapping() const;
		void ApplyPendingDynamics();
		void CheckPhysXErrors(bool bBetweenFrames);
		PtBodyState MakeState(const FPhysXBody& Body) const;
		void MarkDirty(uint32 BodyIndex);
		bool IsAsleep(const FPhysXBody& Body) const;
		// Kept sleeping contacts (as the Jolt backend, see the file comment).
		bool IsSleepingPair(const FPhysXRawContact& Contact) const;
		void UpdateSleepingContacts();
		void FlushKeptContactCommands();
		void NoteKeptContactCommand(uint32 BodyIndex, bool bPutToSleep, bool bPoseSet);
		// I_world^-1 * V (native units) of a dynamic body: R diag(invI) R^T V, R = pose rotation * mass frame rotation.
		physx::PxVec3 MultiplyWorldInverseInertia(const physx::PxRigidDynamic& Actor, const physx::PxVec3& V) const;
		physx::PxVec3 MultiplyWorldInertia(const physx::PxRigidDynamic& Actor, const physx::PxVec3& V) const;
		// Joints (M9, PhysXConstraint.cpp).
		void ReleaseJointsOf(uint32 BodyIndex);       // before the body's actor is released
		void ReleaseAllJoints();                      // the destructor, before the actors
		PtTransform GetActorPose(uint32 BodyIndex) const;   // the engine's actor pose (cm); identity for PT_INDEX_NONE (the world)
		PtJointSnapshot MakeJointSnapshot(uint32 JointIndex) const;
		void StageJointSnapshots();
		void AppendJointSettings(::PhysicsTest::Abi::FTextWriter& Out) const;
		void AppendVisualizationSettings(::PhysicsTest::Abi::FTextWriter& Out) const;

		PtSceneDesc Desc;
		FPhysXSceneConfig Config;
		::PhysicsTest::Abi::FUnitScale Scale;
		physx::PxScene* Scene = nullptr;
		physx::PxCpuDispatcher* Dispatcher = nullptr;
		std::unique_ptr<FPhysXCPUDispatcherSingleThread> SingleThreadDispatcher;
		std::unique_ptr<FPhysXCPUDispatcherInline> InlineDispatcher;
		physx::PxDefaultCpuDispatcher* PoolDispatcher = nullptr;
		std::unique_ptr<FPhysXSimEventCallback> EventCallback;   // viz runs / PT_INSTR_CONTACTS only
		void* ScratchBlock = nullptr;

		physx::PxArray<physx::PxMaterial*> Materials;
		physx::PxArray<PtPhysicalMaterial> MaterialInfos;
		physx::PxArray<FPhysXBody> Bodies;
		physx::PxArray<uint8> MobilityRanks;          // per body, for CanonicalizeContact
		physx::PxArray<uint32> Kinematics;            // indices of kinematic bodies
		physx::PxArray<uint32> DirtyBodies;
		physx::PxArray<uint32> PendingImpulseBodies;  // reserved to the body count: no allocation at T_push
		physx::PxArray<uint32> PendingForceBodies;
		physx::PxArray<uint32> PrevActive;
		physx::PxArray<uint32> ReadScratch;
		physx::PxArray<uint32> ActiveScratch;
		physx::PxArray<PtParticleSnapshot> SnapshotParticles;
		physx::PxArray<PtContactPoint> SnapshotContacts;
		physx::PxArray<FPhysXJoint> Joints;               // M9: one record per joint index (never reused)
		physx::PxArray<PtJointSnapshot> SnapshotJoints;
		physx::PxArray<PtDebugDrawCmd> SnapshotCommands;   // M9: the native draw of the last capture (viz runs)
		physx::PxArray<FPhysXRawContact> SleepingContacts;
		physx::PxArray<FPhysXRawContact> SleepingScratch;
		// The step before the last one: PhysX reports nothing for the pairs of an island already in the step the island
		// falls asleep in, so a newly sleeping pair is taken from here (UpdateSleepingContacts).
		physx::PxArray<FPhysXRawContact> PrevStepContacts;
		physx::PxArray<uint32> ReportStamps;      // per body: the step stamp of its last listener report
		uint32 ReportStamp = 0;
		physx::PxArray<uint32> KeptContactBodies;
		uint32 NumGyroscopicIgnored = 0;   // PT_BODY_GYROSCOPIC requests a GyroscopicForces=Ignore variant ignored (M6)
		physx::PxArray<physx::PxTransform> PreStepPoses;   // viz / PT_INSTR_CONTACTS: the actor poses of the step's narrow phase (M6 (c))
		bool bSleepingContactsDropped = false;

		FPhysXFilterShaderRules FilterRules;   // PxSceneDesc::filterShaderData (PhysX copies it at createScene)
		FPhysXErrorCounts ErrorsSeen;
		uint32 ListenerCapacity = 0;
		uint32 NumStartAsleepSkipped = 0;
		uint32 NumIterationOverrides = 0;
		uint32 NumDepenetrationFromProject = 0;   // UE4Project: unset bodies given the project value
		uint32 NumDepenetrationUnlimited = 0;     // ... whose value became PX_MAX_F32 (0 = unlimited in UE4)
		uint32 NumDepenetrationUnrepresentable = 0;   // SdkDefault: a body value 0 (PhysX needs > 0), left at the default
		uint32 FrameErrorBits = 0;
		uint32 PendingErrorBits = 0;
		uint32 TotalErrorBits = 0;
		uint32 ReadStamp = 0;
		uint64 TotalSteps = 0;
		float StepDeltaTime = 0.0f;
		float LastSimulatedDeltaTime = 0.0f;   // the dt of the last simulate that ran (PxConstraint::getForce divides by it)
		PtVec3 Gravity = {};
		uint32 NumJointsWithoutDynamicBody = 0;   // accepted joints without an engine joint (docs/Abi.md 9.4 item 6)
		uint32 NumJointsReleasedWithBodies = 0;
		FPhysXRenderCounts LastCaptureCommands;    // the last PT_SNAP_NATIVE_DRAW capture (effective physx.debug_visualization)
		uint32 NumNativeCaptures = 0;
		bool bVisualization = false;               // viz run with a non-empty DebugVisualization: the parameters are set
		bool bStepped = false;
		bool bSimulating = false;
		bool bSleepingAllowed = true;
	};

	class FPhysXBackend final : public ::PhysicsTest::Abi::IPhysicsBackend
	{
	public:
		static constexpr const char* ModuleName = "PhysX";

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
		float ToleranceLength = 0.0f;
		float ToleranceSpeed = 0.0f;
		bool bToleranceScaleGiven = false;
	};

	// The fidelity note every PhysX manifest and effective dump carries (02 8.2 "충실도 노트").
	inline constexpr const char* GPhysXFidelityNote = "UE4 settings on PhysX 5.11: UE4 shipped PhysX 3.4.0.0; in 5.11 improved patch friction is always on, "
		"joint projection and the eSWING drive are gone, PxCooking is gone (PxCreateConvexMesh), max depenetration must be > 0, the default broad phase is "
		"PABP";

	// Harness test key (runtime SetOption only, rejected as a CreateScene option): reports one PhysX eINVALID_OPERATION
	// through the foundation's error callback, so the propagation of a PhysX error report into the engine error bits is a
	// unit test.
	inline constexpr const char* GPhysXTestReportErrorKey = "pt.test.report_error";
}
