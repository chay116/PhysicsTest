// PhysScene_Bullet.h - FPhysScene_Bullet and FBulletBackend: Bullet 3.25 behind the C ABI (02_Architecture 8.4, A.15).
// [MVP M5]   PT_BACKEND_NS=Bullet
//
// World (02 8.4): btDefaultCollisionConfiguration (pool sizes from the capacity hints), btCollisionDispatcher (a subclass
// that counts pool overflows), btDbvtBroadphase, btSequentialImpulseConstraintSolver, btDiscreteDynamicsWorld, single
// threaded (BT_THREADSAFE=0). One harness step is stepSimulation(dt, 0, dt): maxSubSteps 0 = one variable step of exactly
// dt (btDiscreteDynamicsWorld.cpp:399-415 [V]). Units: metres (BulletMath.h, D1). No motion states: the adapter reads the
// bodies' world transforms itself.
//
// Shapes (DL42, DL47, PtShapeWrapping.h): btSphereShape, btBoxShape (its margin is per shape: the ctor's setSafeMargin
// gives min(0.04 m, 0.1 x smallest half extent), btBoxShape.cpp:17-25, btConvexInternalShape.h:66-81 [V]; the margin lies
// inside the box, so the box keeps its full extents; shape.box_margin reads getMargin() back), btCapsuleShapeZ (no axis
// remap: Bullet has a Z capsule, btCapsuleShape.h:128-131 [V], the D1 amendment). A body is its native primitive when it
// has one element whose frame and COM frame are the actor frame; otherwise a btCompoundShape with the children at
// inverse(ComLocal) * Elem (ClassifyCompoundWrapping). shape.wrapping counts what the bodies hold (M3.BoxPile: 0).
//
// Body creation follows UE's FInitBodiesHelper order (02 6, DL38; BodyInstance.cpp:1669, 1695-1706, 4648-4743 [V]):
//   1 per body   btRigidBody with the creation-time flags (static / kinematic collision flags, BT_DISABLE_WORLD_GRAVITY,
//                gyroscopic flag, CCD) and the harness mass properties (setMassProps(m, I x 1e-4) in the principal frame).
//                Bullet takes the mass at construction, which UE sets in InitDynamicProperties: nothing steps in between,
//                so the result is the same (as Jolt's MassPropertiesOverride, M3)
//   2 per body   shape, filter (group = 1 << ObjectType, mask = BlockMask: Bullet's broad-phase test is UE's mutual block,
//                btOverlappingPairCache.h needsBroadphaseCollision [V]), material (friction / restitution / combine modes)
//   3 once       btDiscreteDynamicsWorld::addRigidBody for the whole batch (Bullet has no batch add: one loop, after every
//                body of the batch exists)
//   4 per body   InitDynamicProperties in UE order: damping, max angular velocity (both through the internal pre-tick
//                below), max depenetration velocity (no per-body rate in Bullet: recorded), initial velocity, sleep (native
//                Bullet thresholds; the UE material thresholds are M9) - and, when the scene disables sleeping
//                (Normalized), DISABLE_DEACTIVATION (DL48)
//   5 per body   !PT_BODY_START_AWAKE and dynamic: ISLAND_SLEEPING with zero velocity (skipped and counted when the scene
//                disables sleeping)
//
// UE behaviour Bullet does not have, emulated by the internal pre-tick callback (btDiscreteDynamicsWorld.cpp:456-459 [V];
// it runs inside stepSimulation after saveKinematicState and applyGravity, before predictUnconstraintMotion):
//   - damping in UE's order (DampingModel=UE, the default): Bullet damps BEFORE its solver adds the force impulse, UE / Chaos
//     after (PtDamping.h). The pre-tick sets v := UEOrderPreSolveVelocity(v, a dt, max(0, 1 - c dt)) with Bullet's own
//     damping at 0, so the solver's "+ a dt" ends at UE's (v + a dt)(1 - c dt). DampingModel=Converted keeps Bullet's
//     damping with the 02 8.1 coefficient c_b = 1 - (1 - c dt)^(1/dt) and Bullet's order.
//     Known limit (M6, M5 review): the emulation takes the solver's end velocity as W + tau invI dt. A body with a
//     BT_ENABLE_GYROSCOPIC_FORCE_* flag also gets the gyroscopic impulse added to m_externalTorqueImpulse by the solver, so
//     its angular damping order and the angular clamp miss that term; it matters only when PT_BODY_GYROSCOPIC and angular
//     damping or a clamp are both active (both profiles keep gyroscopic off). The fix, if a scene needs it, is to add
//     computeGyroscopicImpulseImplicit_Body / _World(dt) to the predicted end velocity
//   - the per-body max angular (UEParity 62.83 rad/s, DL16) and linear speeds, applied to that end velocity as Chaos clamps
//     after damping. Bullet's own limit is not a velocity clamp: above ANGULAR_MOTION_THRESHOLD = pi/4 per step (47.1 rad/s
//     at 60 Hz, below UEParity's 62.83) btTransformUtil::integrateTransform caps the exponential map's angle but keeps the
//     unclamped axis length, so a step turns the body by 2 atan((|w| dt / (pi/4)) tan(pi/8)) instead of |w| dt - 57.9 deg
//     instead of 60 deg at 62.83 rad/s, never more than 180 deg (btTransformUtil.h:19, 44-72 [V]); the velocity stays.
//     (02 8.4 / DL16 cite btRigidBody::integrateVelocities' pi/(2 dt) = 94.2 rad/s, btRigidBody.cpp:380-385; that function
//     is only called by btSimpleDynamicsWorld, never by btDiscreteDynamicsWorld [V], M5.) The threshold is recorded
//     COMPILE_TIME and the post-tick callback counts the body-steps above it (bullet.angular_motion_limit_hits)
//   - kinematic velocities: StartFrame moves a kinematic body to its target (world transform) and the pre-tick sets the
//     velocity that reaches it, computed in double (Bullet's own saveKinematicState differentiates float quaternions:
//     acos(w) gives ~0.04 rad/s of spin for an unrotated body); without a target the velocity is zero (Chaos
//     EKinematicTargetMode::Reset, PBDRigidsEvolutionGBF.cpp:1191-1197 [V])
//
// Commands (docs/Abi.md 5.10, 7.1): impulses / velocity changes / forces are deferred to StartFrame exactly as in Jolt
// (btRigidBody::applyCentralImpulse changes the velocity at once, btRigidBody.h:319-321 [V]); the force goes through
// applyCentralForce / applyTorque at StartFrame for a body awake then, and stepSimulation clears it (clearForces). A
// command that sets a pose (SET_GLOBAL_POSE, SET_BODY_STATE) also drops the body's contact manifolds
// (btOverlappingPairCache::cleanProxyFromPairs): their points would be stale, and a pair asleep afterwards keeps none.
//
// Reads: ACTIVE_ONLY walks the non-static bodies (Bullet keeps no active list; its own synchronizeMotionStates walks the
// same set) and reports the awake ones, the ones awake at the previous read and the ones a command changed.
//
// Debug snapshot (PROVISIONAL(M9)): contacts are read from the dispatcher's persistent manifolds at capture time - no
// callback runs in the step, so listeners.installed stays 0 - in viz runs or with PT_INSTR_CONTACTS only (the same rule as
// Jolt's listener). Bullet's manifold point is already in Chaos' signs (PtContactConvention.h). The applied normal and
// friction impulses are real (CONTACT_NORMAL_IMPULSE / FRICTION_IMPULSE, kg*cm/s) and "active" is exact
// (PT_CONTACT_ACTIVE: the last solve applied an impulse). Bullet keeps the manifolds of sleeping pairs untouched (both
// bodies inactive: no narrow phase, btCollisionDispatcher::needsCollision [V]), so a sleeping island's points carry their
// last solver results plus PT_CONTACT_SLEEPING - the Jolt rule of M4, with no extra bookkeeping. Particles carry Bullet's
// island tag (NATIVE_ISLANDS; Bullet's islands merge broad-phase pairs, btSimulationIslandManager::findUnions [V]).
//
// ODR (docs/Abi.md 5.13): every adapter container is a btAlignedObjectArray (Bullet's own type); no std:: container.
#pragma once

#include "BulletIncludes.h"
#include "PhysicsTestAbi/PtBackendKit.h"
#include "PtShapeWrapping.h"
#include "PtTypes.h"

namespace PhysicsTest::BulletBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	enum class EDampingModel : uint8
	{
		UEOrder,     // the pre-tick emulation of UE's order (default)
		Converted,   // Bullet's own damping with c_b = UEDampingToBullet(c, dt) (02 8.1), Bullet's order
	};

	// Scene options ([PhysicsTest.Backend.Bullet] keys of a backend variant, case-insensitive; lengths in cm). Defaults are
	// Bullet 3.25's own (btContactSolverInfo.h:77-113, btRigidBody.h:154-155, btCollisionWorld.cpp:71 [V]).
	struct FBulletSceneOptions
	{
		int32 NumIterations = 10;                          // btContactSolverInfo::m_numIterations
		int32 SolverMode = SOLVER_USE_WARMSTARTING | SOLVER_SIMD;   // m_solverMode (0x104)
		bool bSplitImpulse = true;                         // m_splitImpulse
		double SplitImpulsePenetrationThresholdCm = -4.0;  // m_splitImpulsePenetrationThreshold -0.04 m
		double SplitImpulseTurnErp = 0.1;                  // m_splitImpulseTurnErp
		double Erp2 = 0.2;                                 // m_erp2 (contacts)
		double LinearSlopCm = 0.0;                         // m_linearSlop
		double WarmstartingFactor = 0.85;                  // m_warmstartingFactor
		double LinearSleepingThresholdCm = 80.0;           // btRigidBodyConstructionInfo 0.8 m/s
		double AngularSleepingThreshold = 1.0;             // 1 rad/s
		bool bForceUpdateAllAabbs = true;                  // btCollisionWorld::m_forceUpdateAllAabbs
		EDampingModel DampingModel = EDampingModel::UEOrder;
		uint32 ManifoldPoolSize = 0;                       // 0: MaxContactConstraintsHint, else Bullet's 4096
		uint32 AlgorithmPoolSize = 0;                      // 0: MaxBodyPairsHint, else Bullet's 4096
	};

	// One [PhysicsTest.Backend.Bullet] key (CreateScene options, SetOption). PT_OK, or PT_E_INVALID_ARG with a message in
	// Error (an unknown key or a malformed value).
	PtResult ApplySceneOption(FBulletSceneOptions& Options, ::PhysicsTest::Abi::FStringRef Key, ::PhysicsTest::Abi::FStringRef Value, char (&Error)[256]);

	// The collision dispatcher with pool accounting: Bullet falls back to the heap when a pool is exhausted
	// (btCollisionDispatcher.cpp:79-93, 268-277 [V]); every such allocation is counted here and becomes a capacity error bit
	// of the step it happened in (02 9.4, G12).
	class FBulletCollisionDispatcher final : public btCollisionDispatcher
	{
	public:
		explicit FBulletCollisionDispatcher(btCollisionConfiguration* Configuration)
			: btCollisionDispatcher(Configuration)
		{
		}
		btPersistentManifold* getNewManifold(const btCollisionObject* Body0, const btCollisionObject* Body1) override
		{
			if (m_persistentManifoldPoolAllocator->getFreeCount() == 0)
			{
				++NumManifoldHeapAllocations;
			}
			return btCollisionDispatcher::getNewManifold(Body0, Body1);
		}
		void* allocateCollisionAlgorithm(int Size) override
		{
			if (m_collisionAlgorithmPoolAllocator->getFreeCount() == 0)
			{
				++NumAlgorithmHeapAllocations;
			}
			return btCollisionDispatcher::allocateCollisionAlgorithm(Size);
		}
		const btPoolAllocator& GetManifoldPool() const { return *m_persistentManifoldPoolAllocator; }
		const btPoolAllocator& GetAlgorithmPool() const { return *m_collisionAlgorithmPoolAllocator; }

		uint64 NumManifoldHeapAllocations = 0;
		uint64 NumAlgorithmHeapAllocations = 0;
	};

	struct FBulletBody
	{
		btRigidBody* Body = nullptr;             // owns its shape (and a compound's children); nullptr once released
		// The body frame in actor space: the COM principal frame for a dynamic body, identity for static and kinematic
		// bodies (their mass never enters the solver; a kinematic body's velocity is its origin's, PtBodyState).
		PtTransform ComLocal = {};
		PtTransform ComLocalInverse = {};
		PtTransform HarnessComLocal = {};        // PtActorCreateInfo::CenterOfMassLocal as given (ReadMassProperties)
		PtTransform FirstElementLocal = {};      // the first element's actor-space transform: the COM readback of a compound (M6)
		uint32 Motion = PT_MOTION_STATIC;
		uint32 Flags = 0;
		uint32 MaterialIndex = 0;
		double Mass = 0.0;                       // harness values (ReadMassProperties of static / kinematic bodies)
		PtVec3 InertiaDiagonal = {};
		double LinearDamping = 0.0;              // UE coefficients, 1/s
		double AngularDamping = 0.0;
		double MaxAngularVelocity = 0.0;         // rad/s, <= 0 = none
		double MaxLinearVelocity = 0.0;          // cm/s, <= 0 = none
		PtTransform KinematicTarget = {};        // cm, actor pose
		PtTransform KinematicPose = {};          // cm, the pose of the last target / teleport (double: velocities are exact)
		PtVec3 KinematicOriginVelocity = {};     // cm/s: the actor origin's (target - start) / dt of the last move (PtBodyState)
		PtVec3 KinematicAngularVelocity = {};    // rad/s of the last move
		// Deferred to the next step (StartFrame), Bullet units (m/s, rad/s, N, N*m; ACCEL_CHANGE already applied).
		PtVec3 PendingLinearVelocity = {};
		PtVec3 PendingAngularVelocity = {};
		PtVec3 PendingForce = {};
		PtVec3 PendingTorque = {};
		uint32 LastReadStamp = 0;                // dedupes one PT_READ_ACTIVE_ONLY report
		bool bComIdentity = true;
		bool bReleased = false;
		bool bDirty = true;                      // changed by a command since the previous read
		bool bReportedActive = false;            // what the previous read saw (ACTIVE_ONLY reports the transitions)
		bool bHasKinematicTarget = false;
		bool bKinematicMovedLastStep = false;
		bool bPendingImpulse = false;            // listed in FPhysScene_Bullet::PendingImpulseBodies
		bool bPendingForce = false;              // listed in FPhysScene_Bullet::PendingForceBodies
		bool bDirtyListed = false;               // listed in FPhysScene_Bullet::DirtyBodies
		bool bWokenByCommand = false;            // woken by a command since the last step: its sleeping pairs leave the snapshot (M6 (b));
		                                         // listed in FPhysScene_Bullet::WokenByCommandBodies
	};

	class FPhysScene_Bullet final : public ::PhysicsTest::Abi::IPhysicsBackendScene
	{
	public:
		FPhysScene_Bullet(const PtSceneDesc& Desc, const FBulletSceneOptions& Options);
		~FPhysScene_Bullet() override;

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

		// btDiscreteDynamicsWorld internal tick callbacks (the world user info is this scene).
		void OnPreTick(btScalar TimeStep);
		void OnPostTick(btScalar TimeStep);

	private:
		btCollisionShape* MakeElementShape(const PtShapeElem& Element) const;
		btCollisionShape* MakeBodyShape(const PtActorCreateInfo& Actor, const ::PhysicsTest::Abi::TStridedView<PtShapeElem>& Shapes, ::PhysicsTest::Abi::EShapeWrapKind& OutKind) const;
		static void DestroyShape(btCollisionShape* Shape);
		void DestroyBody(FBulletBody& Record);
		void ApplySolverSettings();
		void ApplyPendingDynamics();
		void ApplyConvertedDamping(float StepDt);
		// The pose / velocity of a body as the ABI reports it (actor origin, cm; a kinematic body's origin velocity).
		PtBodyState MakeState(const FBulletBody& Record) const;
		bool IsActiveForRead(const FBulletBody& Record) const;
		void SetBodyPose(FBulletBody& Record, const PtTransform& ActorPose);
		void DropContacts(const FBulletBody& Record);
		void MarkDirty(uint32 BodyIndex);
		// The capacity error bits of the pool overflows since the last call (FBulletCollisionDispatcher counters).
		uint32 TakePoolErrorBits();
		// Counts the wrappers the live bodies' shapes hold (shape.wrapping readback).
		::PhysicsTest::Abi::FShapeWrappingCounts ReadWrapping() const;

		PtSceneDesc Desc;
		FBulletSceneOptions Options;
		btDefaultCollisionConfiguration* CollisionConfiguration = nullptr;
		FBulletCollisionDispatcher* Dispatcher = nullptr;
		btDbvtBroadphase* Broadphase = nullptr;
		btSequentialImpulseConstraintSolver* Solver = nullptr;
		btDiscreteDynamicsWorld* World = nullptr;

		// btAlignedObjectArray only (docs/Abi.md 5.13).
		btAlignedObjectArray<PtPhysicalMaterial> Materials;
		btAlignedObjectArray<FBulletBody> Bodies;
		btAlignedObjectArray<uint8> MobilityRanks;        // per body, for CanonicalizeContact
		btAlignedObjectArray<uint32> NonStaticBodies;     // dynamic and kinematic indices (reads)
		btAlignedObjectArray<uint32> DynamicBodies;       // dynamic indices (gravity changes, step stats, post-tick)
		btAlignedObjectArray<uint32> Kinematics;          // kinematic indices (targets, pre-tick velocities)
		btAlignedObjectArray<uint32> PreTickBodies;       // dynamic bodies with UE damping or a speed limit (pre-tick)
		btAlignedObjectArray<uint32> DirtyBodies;         // changed by a command since the previous read (static ones too)
		btAlignedObjectArray<uint32> PendingImpulseBodies;   // deferred velocity changes (reserved: no allocation at push)
		btAlignedObjectArray<uint32> PendingForceBodies;     // deferred forces (reserved)
		btAlignedObjectArray<uint32> WokenByCommandBodies;   // FBulletBody::bWokenByCommand set since the last step (reserved)
		btAlignedObjectArray<uint32> ReadScratch;         // the report list of one ACTIVE_ONLY read (reserved)
		btAlignedObjectArray<PtParticleSnapshot> SnapshotParticles;
		btAlignedObjectArray<PtContactPoint> SnapshotContacts;
		btAlignedObjectArray<uint8> IslandScratch;

		uint32 ReadStamp = 0;
		uint32 NumStartAsleepSkipped = 0;
		uint32 NumIterationOverrides = 0;
		uint32 NumDepenetrationOverrides = 0;
		uint32 FrameErrorBits = 0;
		uint32 TotalErrorBits = 0;
		uint64 ManifoldHeapSeen = 0;            // FBulletCollisionDispatcher counters at the last TakePoolErrorBits
		uint64 AlgorithmHeapSeen = 0;
		uint64 TotalSteps = 0;
		uint64 AngularMotionLimitHits = 0;      // body-steps whose rotation integrateTransform limited (post-tick)
		int32 PeakManifoldsUsed = 0;
		int32 PeakAlgorithmsUsed = 0;
		float StepDeltaTime = 0.0f;
		float DampingDeltaTime = 0.0f;          // the dt the Converted damping coefficients were made for
		PtVec3 Gravity = {};
		bool bStepped = false;
		bool bSleepingAllowed = true;
		bool bCaptureContacts = false;          // viz runs / PT_INSTR_CONTACTS (the snapshot reports contacts)
	};

	// Module options ([PhysicsTest.Backend.Bullet.Init] keys, InitGamePhys only): Bullet's process-wide globals.
	enum class ECombineRule : uint8
	{
		UE,       // BulletCombine.h: PtCombine (the larger UE mode), dynamic value only (DL41)
		Bullet,   // Bullet's own callbacks: F0 * F1 clamped to 10, R0 * R1
	};

	struct FBulletModuleOptions
	{
		ECombineRule CombineRule = ECombineRule::UE;
		double DeactivationTime = 2.0;              // s > 0, gDeactivationTime (btRigidBody.cpp:25 [V]); 0 would disable sleep
		// gContactBreakingThreshold as Bullet takes it (btPersistentManifold.cpp:26 [V], default 0.02). Two roles: under the
		// dispatcher's default CD_USE_RELATIVE_CONTACT_BREAKING_THRESHOLD (btCollisionDispatcher.cpp:32 [V]) a unitless factor of
		// each shape's angular motion disc for the manifold breaking threshold (btCollisionDispatcher.cpp:74-75,
		// btCollisionShape.cpp:42-45 [V]); and, in m, the broad-phase AABB inflation (btCollisionWorld.cpp:153-160 [V]).
		double ContactBreakingThreshold = 0.02;
	};

	class FBulletBackend final : public ::PhysicsTest::Abi::IPhysicsBackend
	{
	public:
		static constexpr const char* ModuleName = "Bullet";

		void GetInfo(PtBackendInfo& Out) const override;
		PtResult InitGamePhys(const PtInitParams& Params) override;
		PtResult TermGamePhys() override;
		PtResult CreateScene(const PtSceneDesc& Desc, ::PhysicsTest::Abi::IPhysicsBackendScene*& OutScene) override;
		void DestroyScene(::PhysicsTest::Abi::IPhysicsBackendScene* Scene) override;
		PtResult SetModuleOption(::PhysicsTest::Abi::FStringRef Key, ::PhysicsTest::Abi::FStringRef Value) override;
		PtResult DumpModuleSettings(::PhysicsTest::Abi::FTextWriter& Out) override;
		PtResult GetMemoryStats(PtMemoryStats& Out) override;

	private:
		FBulletModuleOptions ModuleOptions;
		uint32 NumLiveScenes = 0;
		uint32 MaxThreads = 1;
		// The globals as they were before InitGamePhys (restored by TermGamePhys).
		CalculateCombinedCallback SavedFrictionCallback = nullptr;
		CalculateCombinedCallback SavedRestitutionCallback = nullptr;
		btScalar SavedDeactivationTime = 0;
		btScalar SavedContactBreakingThreshold = 0;
		bool bGlobalsInstalled = false;
	};

	// Engine error bits of PtFrameResult / PtStepStats (the harness's own: Bullet reports no errors itself). Both are
	// capacity bits (bench exit code 5, G12), published as the effective key errors.capacity_bits (docs/Abi.md 5.14).
	inline constexpr uint32 GBulletErrorManifoldPoolFallback = 1u << 0;    // a contact manifold came from the heap
	inline constexpr uint32 GBulletErrorAlgorithmPoolFallback = 1u << 1;   // a collision algorithm came from the heap
	inline constexpr uint32 GBulletCapacityErrorBits = GBulletErrorManifoldPoolFallback | GBulletErrorAlgorithmPoolFallback;

	// ANGULAR_MOTION_THRESHOLD of btTransformUtil.h:19 [V]: above this angle per step integrateTransform under-rotates (see the
	// header comment).
	inline constexpr double GBulletAngularMotionThreshold = 0.5 * 1.5707963267948966;

	// Bullet's heap traffic while the module is initialised (M7 fix round 0): InitGamePhys installs counting wrappers with
	// btAlignedAllocSetCustom (every btAlignedAlloc / btAlignedFree goes through them), TermGamePhys restores Bullet's
	// defaults. Process-wide counts, as PhysX's allocator callback: PtMemoryStats.NumAllocs / NumFrees.
	int64 GetBulletHeapAllocCount();
	int64 GetBulletHeapFreeCount();
}
