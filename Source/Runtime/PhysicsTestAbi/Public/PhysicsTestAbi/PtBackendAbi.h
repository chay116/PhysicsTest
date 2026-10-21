/* PtBackendAbi.h - pure C11. The ONLY header shared with the Chaos DLL (02_Architecture 7, A.1, docs/Abi.md).
   Units: cm, kg, s, rad, Z-up, double.
   v0.9 (major 0) until the M6 freeze. CORE entries freeze as v1.0 at M6. Entries marked PROVISIONAL(M9) (joints,
   debug snapshot, native draw) may still change until M9; every such change bumps PT_ABI_PROVISIONAL_REVISION.

   Calling rules (enforced by PtBackendKit.h on the backend side, docs/Abi.md):
     - every entry returns PtResult; no C++ exception crosses the boundary (SEH is not swallowed)
     - every call comes from the thread that called InitGamePhys; any other thread gets PT_E_WRONG_THREAD and the
       call has no side effect (the log callback is the only thing a backend may call from any thread)
     - module: Loaded -> InitGamePhys -> Ready -> TermGamePhys -> Terminated (PT_MODULE_NO_REINIT: no second Init).
       Outside Ready only GetInfo, InitGamePhys and GetLastErrorMessage work; every other entry is PT_E_BAD_STATE
       (a NULL argument may be reported first as PT_E_INVALID_ARG)
     - scene phase: Idle -> SetUpForFrame -> SetUp -> StartFrame -> Started -> WaitPhysScenes -> Waited -> EndFrame
       -> Idle; creation, writes and reads only in Idle; anything else is PT_E_BAD_STATE without a side effect
     - buffers are owned by the caller. Too small: PT_E_CAPACITY, the required count (or bytes incl. the NUL) is
       written back and nothing else is written. GetLastErrorMessage and PrepareForSimulation truncate instead and
       return PT_W_TRUNCATED
     - batches are all-or-nothing: an invalid element rejects the whole call before anything changes
     - input structs carry StructSize; a value below this header's sizeof() is PT_E_INVALID_ARG (a larger one, from
       a newer host, is accepted and the unknown tail ignored) */
#ifndef PT_BACKEND_ABI_H
#define PT_BACKEND_ABI_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
#  define PT_STATIC_ASSERT(c, m) static_assert(c, m)
extern "C" {
#else
#  define PT_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif

/* v0.9 (major 0) until the M6 freeze; the abi-v1.0 tag sets MAJOR 1 / MINOR 0. v1.x is append-only.
   The host always calls PT_GetBackendApi(PT_ABI_VERSION_MAJOR) with the major it was built with,
   so the M2.5 spike DLL (built against major 0) and the host agree. */
#define PT_ABI_VERSION_MAJOR 0u
#define PT_ABI_VERSION_MINOR 9u
#define PT_ABI_VERSION ((PT_ABI_VERSION_MAJOR << 16) | PT_ABI_VERSION_MINOR)
#define PT_ABI_PROVISIONAL_REVISION 1u                 /* PROVISIONAL(M9) layout revision; frozen at M9 */
#define PT_GET_BACKEND_API_SYMBOL "PT_GetBackendApi"   /* the only export of a DLL backend */
#define PT_CALL __cdecl
#define PT_INDEX_NONE 0xFFFFFFFFu
/* 32 channels with uint32 masks is a deliberate Phase-1 limit. UE 5.8.3 has ECC_MAX = 65 with uint64 masks
   (EngineTypes.h:1101-1176, CollisionFilterData.cpp:263-272,310-313 [V]). 32 is what UE4's legacy PhysX word3
   (5-bit channel field) and Bullet's int group/mask can hold. The Chaos bridge widens BlockMask to uint64.
   The harness C++ constant is PhysicsTest::NumCollisionChannels (a different name, so that this macro cannot
   rewrite its declaration); host code static_asserts that the two agree. */
#define PT_NUM_COLLISION_CHANNELS 32u
/* PtSceneDesc.AsyncFixedTimeStepSize for sync stepping. Never 0: Chaos treats AsyncDt >= 0 as async
   (IsUsingAsyncResults, PhysicsSolverBase.h:712-715 [V]). */
#define PT_ASYNC_DT_SYNC (-1.0)

typedef int32_t PtResult;
#define PT_OK                 0
#define PT_W_TRUNCATED        1   /* success; a text output was cut to the caller's capacity (Length = bytes needed) */
#define PT_E_INVALID_ARG     -1
#define PT_E_INVALID_HANDLE  -2
#define PT_E_BAD_STATE       -3
#define PT_E_WRONG_THREAD    -4
#define PT_E_UNSUPPORTED     -5
#define PT_E_CAPACITY        -6   /* the required count is written back */
#define PT_E_VERSION         -7
#define PT_E_ENGINE          -8
#define PT_E_OUT_OF_MEMORY   -9
#define PT_E_TRANSPORT      -10
#define PT_E_INTERNAL       -11

/* ---- Log levels of PtLogCallback (same order as UE ELogVerbosity) ---- */
#define PT_LOG_FATAL    1u
#define PT_LOG_ERROR    2u
#define PT_LOG_WARNING  3u
#define PT_LOG_DISPLAY  4u
#define PT_LOG_LOG      5u
#define PT_LOG_VERBOSE  6u

typedef uint64_t PtSceneId;                                         /* 0 = invalid; (generation << 32) | slot */
typedef struct PtVec3 { double X, Y, Z; } PtVec3;                   /* 24 B */
typedef struct PtQuat { double X, Y, Z, W; } PtQuat;                /* 32 B */
typedef struct PtTransform { PtQuat Rotation; PtVec3 Translation; } PtTransform;  /* 56 B; scale is baked in by the host */
typedef struct PtTextBuffer { char* Data; uint32_t Capacity; uint32_t Length; } PtTextBuffer;  /* Length includes the NUL */
typedef struct PtKeyValue { const char* Key; const char* Value; } PtKeyValue;     /* "physx.Solver"="PGS", "p.Chaos.*"=".." */

/* ---- Motion type ---- */
#define PT_MOTION_STATIC     0u
#define PT_MOTION_KINEMATIC  1u
#define PT_MOTION_DYNAMIC    2u

/* ---- Object state: same values as Chaos::EObjectStateType (ObjectState.h:9-18 [V]) ---- */
#define PT_OBJECT_STATE_UNINITIALIZED 0u
#define PT_OBJECT_STATE_SLEEPING      1u
#define PT_OBJECT_STATE_KINEMATIC     2u
#define PT_OBJECT_STATE_STATIC        3u
#define PT_OBJECT_STATE_DYNAMIC       4u

/* ---- Shape type ---- */
#define PT_SHAPE_SPHERE   0u
#define PT_SHAPE_BOX      1u   /* half extents (the host halves FKBoxElem's full X/Y/Z) */
#define PT_SHAPE_CAPSULE  2u   /* Radius + HalfLength (segment only, = FKSphylElem::Length / 2) along local Z */
#define PT_SHAPE_CONVEX   3u   /* data model / ABI only in Phase 1 (no headline scene) */
#define PT_SHAPE_TRIMESH  4u   /* reserved */

/* ---- Material combine mode: same values as EFrictionCombineMode and PxCombineMode; the larger value wins ---- */
#define PT_COMBINE_AVERAGE   0u
#define PT_COMBINE_MIN       1u
#define PT_COMBINE_MULTIPLY  2u
#define PT_COMBINE_MAX       3u

/* ---- Joint type: PROVISIONAL(M9) ---- */
#define PT_JOINT_BALL   0u
#define PT_JOINT_HINGE  1u   /* twist about Frame X is free; swings are locked (ConstraintUtils::ConfigureAsHinge) */
#define PT_JOINT_FIXED  2u   /* reserved */

/* ---- Run mode ---- */
#define PT_RUN_BENCH     0u
#define PT_RUN_BEHAVIOR  1u
#define PT_RUN_VIZ       2u

/* ---- Instrumentation tiers: any bit set makes the run non-headline ---- */
#define PT_INSTR_COUNTERS           (1u << 0)
#define PT_INSTR_PHASES             (1u << 1)
#define PT_INSTR_ALLOC              (1u << 2)
#define PT_INSTR_CONTACTS           (1u << 3)
#define PT_INSTR_NATIVE_DEBUG_DRAW  (1u << 4)
#define PT_INSTR_ALL                0x1Fu        /* every defined PT_INSTR_* bit; other bits -> PT_E_INVALID_ARG */

/* ---- Scene simulation flags ---- */
#define PT_SIM_ALLOW_SLEEPING        (1u << 0)   /* off (Normalized): PhysX eDISABLE_SLEEPING, Chaos p.Chaos.Solver.Sleep.Enabled=0,
                                                    Jolt bAllowSleeping=false, Bullet DISABLE_DEACTIVATION */
#define PT_SIM_ENABLE_CCD            (1u << 1)
#define PT_SIM_ENHANCED_DETERMINISM  (1u << 2)
#define PT_SIM_ALL                   0x7u        /* every defined PT_SIM_* bit */

/* ---- Body flags. Creation-time (CreateActor, BodyInstance.cpp:1301-1340 [V]) unless noted ---- */
#define PT_BODY_ENABLE_GRAVITY         (1u << 0)
#define PT_BODY_START_AWAKE            (1u << 1)   /* applied after AddActorsToScene: wake counter 0 + putToSleep when clear;
                                                      skipped when PT_SIM_ALLOW_SLEEPING is off (PhysX rejects putToSleep in an
                                                      eDISABLE_SLEEPING scene, NpRigidDynamic.cpp:521-525 [V]) */
#define PT_BODY_ALLOW_SLEEP            (1u << 2)
#define PT_BODY_USE_CCD                (1u << 3)
#define PT_BODY_GYROSCOPIC             (1u << 4)
#define PT_BODY_INERTIA_CONDITIONING   (1u << 5)
#define PT_BODY_OVERRIDE_ITERATIONS    (1u << 6)
#define PT_BODY_FLAGS_ALL              0x7Fu       /* every defined PT_BODY_* bit */

/* ---- Shape / joint / read flags ---- */
#define PT_SHAPEF_SIMULATION         (1u << 0)
#define PT_SHAPEF_QUERY              (1u << 1)
#define PT_SHAPEF_ALL                0x3u
#define PT_JOINTF_DISABLE_COLLISION  (1u << 0)   /* PROVISIONAL(M9) */
#define PT_READ_ALL          0u   /* dense: one state per created body, in index order (OutIndices may be NULL). A released
                                     body reports ObjectState UNINITIALIZED with a zero pose (identity rotation) and zero
                                     velocities */
#define PT_READ_ACTIVE_ONLY  1u   /* every live body whose state may have changed since the previous read: awake dynamic
                                     bodies, moved kinematic bodies and any body whose ObjectState changed. OutIndices
                                     is required (Capacity > 0); released bodies are never reported */

/* ---- Capabilities ---- */
#define PT_CAP(n) ((uint64_t)1 << (n))
#define PT_CAP_COLLISION                 PT_CAP(0)
#define PT_CAP_SHAPE_CONVEX              PT_CAP(1)
#define PT_CAP_SHAPE_TRIMESH_STATIC      PT_CAP(2)
#define PT_CAP_KINEMATIC                 PT_CAP(3)
#define PT_CAP_JOINT_BALL                PT_CAP(4)   /* PROVISIONAL(M9) */
#define PT_CAP_JOINT_HINGE               PT_CAP(5)   /* PROVISIONAL(M9) */
#define PT_CAP_SLEEPING                  PT_CAP(6)
#define PT_CAP_CCD                       PT_CAP(7)
#define PT_CAP_GYROSCOPIC                PT_CAP(8)
#define PT_CAP_MULTITHREADED             PT_CAP(9)
#define PT_CAP_NATIVE_FRAME_MODEL        PT_CAP(10)  /* honours MaxSubsteps > 1, bSubstepping and AsyncDt; reports NumStepsTaken
                                                        (= the engine steps actually advanced, FStepPlan::NumSteps).
                                                        Chaos; also Null (FStepPlanner semantics) so the contract is gated at M6 (A10) */
#define PT_CAP_ACTIVE_LIST               PT_CAP(11)
#define PT_CAP_STATE_WRITE               PT_CAP(12)
#define PT_CAP_NATIVE_HASH               PT_CAP(13)
#define PT_CAP_CONTACT_POINTS            PT_CAP(16)  /* PROVISIONAL(M9) (debug snapshot) */
#define PT_CAP_CONTACT_NORMAL_IMPULSE    PT_CAP(17)  /* PROVISIONAL(M9) */
#define PT_CAP_CONTACT_FRICTION_IMPULSE  PT_CAP(18)  /* PROVISIONAL(M9) */
#define PT_CAP_CONTACT_ACTIVE_EXACT      PT_CAP(19)  /* PROVISIONAL(M9) */
#define PT_CAP_NATIVE_ISLANDS            PT_CAP(20)  /* PROVISIONAL(M9). Jolt does not declare it: GetIslandIndexInternal is an
                                                        internal, per-step API (MotionProperties.h:258 [V]); the viewer uses
                                                        the harness FIslandBuilder */
#define PT_CAP_SLEEP_THRESHOLD_INFO      PT_CAP(21)
#define PT_CAP_BOUNDS                    PT_CAP(22)  /* PROVISIONAL(M9) */
#define PT_CAP_JOINT_SNAPSHOT            PT_CAP(23)  /* PROVISIONAL(M9) */
#define PT_CAP_NATIVE_DEBUG_DRAW         PT_CAP(24)  /* PROVISIONAL(M9). Bullet: viz runs only - the drawer is attached only in viz
                                                        and DBG_NoDeactivation mirrors the profile (btDiscreteDynamicsWorld.cpp:418-421 [V]) */
#define PT_CAP_PHASE_TIMERS              PT_CAP(25)
#define PT_CAP_ALLOC_STATS               PT_CAP(26)
/* The PROVISIONAL(M9) capabilities: a host reads them as absent when PtBackendInfo.ProvisionalRevision differs. */
#define PT_CAP_PROVISIONAL_MASK (PT_CAP_JOINT_BALL | PT_CAP_JOINT_HINGE | PT_CAP_CONTACT_POINTS | PT_CAP_CONTACT_NORMAL_IMPULSE | \
                                 PT_CAP_CONTACT_FRICTION_IMPULSE | PT_CAP_CONTACT_ACTIVE_EXACT | PT_CAP_NATIVE_ISLANDS | \
                                 PT_CAP_BOUNDS | PT_CAP_JOINT_SNAPSHOT | PT_CAP_NATIVE_DEBUG_DRAW)

/* ---- Module flags ---- */
#define PT_MODULE_ONE_PER_PROCESS         0x01u
#define PT_MODULE_NO_REINIT               0x02u
#define PT_MODULE_UE_RUNTIME              0x04u  /* Chaos DLL. The bench writes and flushes every output plus exit.json, then
                                                    ends with TerminateProcess(GetCurrentProcess(), code); the runner trusts exit.json */
#define PT_MODULE_GLOBAL_SCENE_SETTINGS   0x08u  /* settings fixed process-wide at InitGamePhys. PhysX: one PxPhysics and one
                                                    PxTolerancesScale per process (PxPhysics.h:997, PxSceneDesc.h:541 [V]);
                                                    UE4Compat/-TGS/-Stab share (100,1000) and coexist as separate scenes,
                                                    SdkDefault (1,10) needs its own process. Bullet: global combine callbacks */
#define PT_MODULE_SPAWNS_THREADS_AT_INIT  0x10u
#define PT_MODULE_DEBUG_DRAW_COMPILED     0x20u

typedef void (PT_CALL *PtLogCallback)(void* UserData, uint32_t Level, const char* Category, const char* Message);  /* may be called from any thread */

typedef struct PtBackendInfo {
	uint32_t StructSize; uint32_t ModuleFlags; uint64_t Capabilities;
	const char* Name; const char* DisplayName; const char* EngineVersion; const char* SourceCommit;
	const char* BuildConfig;
	const char* BuildFlagsJson;      /* adapter-TU self-report only (__AVX2__, _M_FP_FAST, _MSC_FULL_VER, _DLL, SDK PUBLIC defines).
                                      SDK-internal flags come from compile_commands.json (CMake) or UBT .rsp files (Chaos) */
	uint32_t NumPhaseTimers;
	uint32_t ProvisionalRevision;    /* = PT_ABI_PROVISIONAL_REVISION the module was built with; on mismatch the host
                                      treats every PROVISIONAL(M9) capability as absent */
	const char* const* PhaseTimerNames;
	uint32_t NumRequiredEffectiveKeys; uint32_t Reserved;
	const char* const* RequiredEffectiveKeys;   /* backend-specific required keys; A7 checks the generic set plus these */
} PtBackendInfo;

typedef struct PtInitParams {
	uint32_t StructSize;
	uint32_t Flags;                  /* bench | viz */
	uint32_t MaxThreads;             /* process-wide budget T. Chaos: -corelimit=T on the PreInit command line */
	uint32_t Reserved;
	PtLogCallback LogCallback; void* LogUserData;
	const char* EngineCommandLine; const char* SavedDir;
	const PtKeyValue* Options; uint32_t NumOptions;   /* process-wide module options only, e.g. the PhysX tolerances scale */
} PtInitParams;

/* Hash split: desc_sha256 covers only engine-independent inputs - materials, shapes, actor create infos, convex
   vertices, constraints, Gravity and the dt sequence (NominalDeltaTime + host plan). Every other PtSceneDesc field
   (RunMode, InstrumentFlags, SimFlags, AsyncFixedTimeStepSize, RestitutionThreshold, MaxLinearVelocity, iterations,
   NumThreads, hints, Options) goes to options_sha256, which may differ per engine; profile files go to
   physics_sha256 / variant_sha256. */
typedef struct PtSceneDesc {
	uint32_t StructSize; uint32_t RunMode; uint32_t InstrumentFlags; uint32_t SimFlags;
	const char* DebugName;
	PtVec3 Gravity;                  /* explicit (0,0,-980); SDK backends set it only when it changes */
	double NominalDeltaTime;
	double AsyncFixedTimeStepSize;   /* PT_ASYNC_DT_SYNC (-1) = sync, > 0 = async fixed dt; anything else -> PT_E_INVALID_ARG.
                                      > 0 needs PT_CAP_NATIVE_FRAME_MODEL (the Harness driver plans async steps itself and
                                      creates the scene with -1).
                                      Chaos AsyncFixed: fixed-dt stepping needs p.UseAsyncInterpolation=1, so the GT state is
                                      interpolated with one-step latency (PhysicsSolverBase.h:717-720, .cpp:591-605 [V]) */
	double RestitutionThreshold;     /* cm/s; < 0 means engine native (recorded) */
	double MaxLinearVelocity;        /* <= 0 means unclamped */
	int32_t PositionIterations, VelocityIterations, ProjectionIterations;   /* 0 = variant default */
	uint32_t NumThreads;             /* T including the caller; 1 = inline.
                                      Chaos T=1:  SetThreadingMode_External(SingleThread) + p.Chaos.SingleWorkerPhysics=1
                                                  (opt. p.Chaos.DisablePhysicsParallelFor=1) + -corelimit=1
                                                  (Parallel.cpp:95,191; ConstraintGroupSolver.cpp:250 [V])
                                      Chaos T>=2: TaskGraph + -corelimit=T + p.Chaos.MaxNumWorkers=T (a cap only;
                                                  Parallel.cpp:16,33 default 100 [V])
                                      effective keys: threads.single_worker, threads.max_num_workers */
	uint32_t MaxBodiesHint, MaxBodyPairsHint, MaxContactConstraintsHint, Reserved0;
	const PtKeyValue* Options; uint32_t NumOptions; uint32_t Reserved1;   /* scene-level variant keys; unknown keys -> PT_E_INVALID_ARG */
} PtSceneDesc;

typedef struct PtPhysicalMaterial {
	double Friction, StaticFriction, Restitution;   /* raw UPhysicalMaterial values, not pre-resolved. Per contact pair:
                                                      C = combine with mode max(M0, M1)
                                                      dynamic = C(F0, F1)
                                                      static  = max(C(max(F0,SF0), max(F1,SF1)), dynamic)
                                                      (PBDCollisionConstraints.cpp:472-476, PBDCollisionConstraint.h:486 [V]).
                                                      Per backend: PhysX SdkDefault createMaterial(max(F,SF), F, R) reproduces
                                                      it (PxPhysics.h:764 [V]); PhysX UE4Compat static = dynamic [M];
                                                      Jolt/Bullet have one coefficient per pair, so their callbacks return
                                                      dynamic only (effective key material.static_friction_rule=dynamic_only) */
	double SleepLinearVelocityThreshold, SleepAngularVelocityThreshold;  /* cm/s, rad/s */
	uint32_t SleepCounterThreshold;
	uint8_t FrictionCombineMode, RestitutionCombineMode;   /* per material, resolved by the host:
                                                            bOverride*CombineMode ? material mode : project setting */
	uint16_t Reserved;
} PtPhysicalMaterial;   /* 48 B */

typedef struct PtShapeElem {
	PtTransform LocalTransform;      /* relative to the actor origin */
	uint32_t Type, MaterialIndex;
	double Radius, HalfLength;       /* capsule debug draw: HalfHeight = HalfLength + Radius */
	PtVec3 BoxHalfExtent;
	uint32_t FirstVertex, NumVertices;
	double ContactOffset;            /* < 0 = profile rule (PhysX UE4 clamp) */
	double RestOffset;
	uint32_t Flags, Reserved;
} PtShapeElem;          /* 136 B */

/* Field groups follow UE. Creation-time (CreateActor) = Motion, Flags, iterations, MaxLinearVelocity (UE uses
   sqrt(FLT_MAX)); shape step = ObjectType/BlockMask filter + material; post-add (InitDynamicProperties, UE order)
   = mass/inertia/COM, damping, MaxAngularVelocity, MaxDepenetrationVelocity, initial velocity,
   sleep/stabilization multipliers, iterations, then putToSleep for !START_AWAKE.
   BodyIndex: creation order; batch i must carry (bodies created so far) + i. */
typedef struct PtActorCreateInfo {
	uint32_t BodyIndex, Motion, Flags, FirstShape, NumShapes;
	uint8_t ObjectType, PositionIterations, VelocityIterations, ProjectionIterations;
	uint32_t BlockMask, Reserved0;   /* 32-channel mask (PT_NUM_COLLISION_CHANNELS) */
	PtTransform ActorTransform;
	PtVec3 InitialLinearVelocity /* at COM, cm/s */, InitialAngularVelocity /* rad/s, world; harness extension */;
	double Mass;                     /* kg, computed by harness BodyUtils */
	PtVec3 InertiaDiagonal;          /* kg*cm^2, principal */
	PtTransform CenterOfMassLocal;   /* COM position + principal axes, in actor space */
	double LinearDamping, AngularDamping;   /* UE semantics: v *= max(0, 1 - c*dt) */
	double MaxAngularVelocity;       /* rad/s; <= 0 = unclamped */
	double MaxLinearVelocity;        /* cm/s; <= 0 = unclamped */
	double MaxDepenetrationVelocity; /* cm/s; the UE value (0 = no limit); < 0 = solver default (PhysX: PX_MAX_F32) */
	double SleepThresholdMultiplier, StabilizationThresholdMultiplier;
	uint64_t UserTag;
} PtActorCreateInfo;    /* 288 B */

typedef struct PtActorBatch {
	uint32_t StructSize;
	uint32_t NumActors;          const PtActorCreateInfo* Actors; uint32_t ActorStride;
	uint32_t NumShapes;          const PtShapeElem* Shapes;       uint32_t ShapeStride;
	uint32_t NumConvexVertices;  const PtVec3* ConvexVertices;
} PtActorBatch;

typedef struct PtConstraintCreateInfo {   /* PROVISIONAL(M9) */
	uint32_t Type;
	uint32_t Body1;                  /* child */
	uint32_t Body2;                  /* parent, or PT_INDEX_NONE for the world */
	uint32_t Flags;
	PtTransform Frame1, Frame2;      /* in each body's actor space */
	double TwistLimitLow, TwistLimitHigh;   /* reserved; NaN */
} PtConstraintCreateInfo;   /* 144 B */

/* ---- Body commands (FChaosEngineInterface::*_AssumesLocked, ChaosEngineInterface.h:358-471 [V]) ----
   Applied in array order at T_push. Frame rules of a NATIVE_FRAME_MODEL backend (UE5 Chaos, docs/Abi.md 5.10):
     - impulses and velocity changes accumulate and enter the integration of the next step that runs
       (PBDRigidsEvolutionGBF.cpp:911-924 [V]); later pushed steps of the same frame do not repeat them
     - forces and torques accumulate until the next frame that pushes steps and then act on EVERY step of that
       frame: Step_External copies the first step's accelerations into each later pushed step and clears only the
       impulses (ChaosMarshallingManager.cpp:105-111, :325-342 [V]). Frames that push nothing (AsyncFixed) carry
       them over; at the push they are scaled by 1 / (frames with dt > 0 since the last push)
       (PhysicsSolverBase.cpp:623-646, PBDRigidsSolver.cpp:1582 [V]). A frame that pushes but runs no step
       (dt <= MinDeltaTime) consumes them
     - a kinematic target is reached at the last step of a solver-substepped frame (step k of N moves to
       Lerp / Slerp(current, target, 1 / (N - k))), otherwise at the first step; the steps after it zero the kinematic
       velocity (PhysicsSolverBase.cpp:250, PBDRigidsEvolutionGBF.cpp:1203-1220 [V]). It stays pending until a step runs
   The Harness driver runs one step per backend frame and sends what each step needs (02 9.1).
   Host convention (both drivers, docs/Abi.md 5.10): UE's AddForce / AddTorque wake the body on the game thread when they
   are called (SingleParticlePhysicsProxy.h:706-720, :757-771 [V]). The host sends that wake as a WAKE_UP at the position
   the force was issued and the force itself, flagged PT_CMDF_NO_WAKE, at the end of the push (Native) or of each step's
   push (Harness). Commands to a released body, and a body index released twice, are rejected by the Kit. */
#define PT_CMD_SET_GLOBAL_POSE          1u   /* Transform = actor pose (teleport) */
#define PT_CMD_SET_KINEMATIC_TARGET     2u   /* Transform = target actor pose (see the frame rules above) */
#define PT_CMD_SET_LINEAR_VELOCITY      3u   /* Vector, cm/s */
#define PT_CMD_SET_ANGULAR_VELOCITY     4u   /* Vector, rad/s */
#define PT_CMD_ADD_IMPULSE              5u   /* AddImpulse_AssumesLocked: kg*cm/s */
#define PT_CMD_ADD_ANGULAR_IMPULSE      6u   /* AddAngularImpulseInRadians_AssumesLocked */
#define PT_CMD_ADD_FORCE                7u   /* AddForce_AssumesLocked (PT_CMDF_ACCEL_CHANGE, PT_CMDF_ALLOW_SUBSTEPPING, PT_CMDF_NO_WAKE) */
#define PT_CMD_ADD_TORQUE               8u   /* AddTorque_AssumesLocked */
#define PT_CMD_WAKE_UP                  9u
#define PT_CMD_PUT_TO_SLEEP            10u
#define PT_CMD_SET_BODY_STATE          11u   /* harness-only full state write (PT_CAP_STATE_WRITE): Transform = pose,
                                                Vector = linear, Vector2 = angular velocity */
#define PT_CMD_ADD_VELOCITY            12u   /* AddVelocity_AssumesLocked: cm/s, mass-independent */
#define PT_CMD_ADD_ANGULAR_VELOCITY    13u   /* AddAngularVelocityInRadians_AssumesLocked */
#define PT_CMDF_ALLOW_SUBSTEPPING  (1u << 0)  /* forces / torques: informational only. UE5 ignores bAllowSubstepping
                                                (ChaosEngineInterface.cpp:932-950, :1024-1037 [V]) and so does every
                                                NATIVE_FRAME_MODEL backend; the host uses it only for the UE4
                                                FPhysSubstepTask rule of the Harness driver (UE4PhysSubstep) */
#define PT_CMDF_ACCEL_CHANGE       (1u << 1)
#define PT_CMDF_AUTO_WAKE          (1u << 2)  /* SET_LINEAR / ANGULAR_VELOCITY: wake a sleeping body. SET_GLOBAL_POSE:
                                                informational for a NATIVE_FRAME_MODEL backend - UE5's teleport always
                                                wakes a sleeping body (SetX, SingleParticlePhysicsProxy.h:240-251 [V]) */
#define PT_CMDF_NO_WAKE            (1u << 3)  /* ADD_FORCE / ADD_TORQUE: do not wake; a body that is asleep when the command
                                                is applied ignores it (the host already sent UE's game-thread wake). Without
                                                it the command is UE's AddForce / AddTorque and wakes a sleeping body */
#define PT_CMDF_ALL                0xFu

typedef struct PtBodyCommand {
	uint32_t Type, Body, Flags, Reserved;
	PtTransform Transform;
	PtVec3 Vector, Vector2;
} PtBodyCommand;        /* 120 B */

typedef struct PtBodyState {
	PtTransform ActorTransform;      /* body ORIGIN, not the COM */
	PtVec3 LinearVelocity;           /* at COM, cm/s */
	PtVec3 AngularVelocity;          /* rad/s */
	uint32_t ObjectState, Flags;
} PtBodyState;          /* 112 B */

typedef struct PtMassProperties { double Mass; PtVec3 InertiaDiagonal; PtTransform CenterOfMassLocal; } PtMassProperties;  /* 88 B */
typedef struct PtFrameResult { uint32_t NumStepsTaken; uint32_t EngineErrorBits; } PtFrameResult;
typedef struct PtStepStats {
	uint32_t StructSize;
	int32_t NumActiveBodies, NumSleepingBodies, NumContactPairs, NumContactPoints, NumIslands;   /* -1 = n/a */
	uint32_t EngineErrorBits, NumPhaseTimes;
	int64_t PhaseTimeNs[8];
} PtStepStats;          /* 96 B; reads that need no listeners are allowed in headline runs */
typedef struct PtMemoryStats { int64_t LiveBytes, PeakBytes, NumAllocs, NumFrees; uint32_t AllocatorKind, Reserved; } PtMemoryStats;

/* ---- Debug snapshot (Chaos conventions): PROVISIONAL(M9) ---- */
#define PT_SNAP_PARTICLES       (1u << 0)
#define PT_SNAP_BOUNDS          (1u << 1)
#define PT_SNAP_CONTACTS        (1u << 2)
#define PT_SNAP_JOINTS          (1u << 3)
#define PT_SNAP_NATIVE_ISLANDS  (1u << 4)
#define PT_SNAP_SLEEP_INFO      (1u << 5)
#define PT_SNAP_NATIVE_DRAW     (1u << 6)
#define PT_CONTACT_ACTIVE           (1u << 0)
#define PT_CONTACT_ACTIVE_APPROX    (1u << 1)   /* Jolt: phi <= 0 */
#define PT_CONTACT_SPECULATIVE      (1u << 2)
#define PT_CONTACT_STATIC_FRICTION  (1u << 3)
#define PT_CONTACT_SWAPPED          (1u << 4)

typedef struct PtParticleSnapshot {
	PtTransform ActorTransform, CenterOfMassWorld;
	PtVec3 BoundsMin, BoundsMax;     /* engine-defined bounds */
	PtVec3 LinearVelocity, AngularVelocity;
	uint32_t BodyIndex; uint8_t ObjectState, Flags; uint16_t Reserved;
	int32_t NativeIslandId; uint32_t Reserved2;
} PtParticleSnapshot;   /* 224 B */

typedef struct PtContactPoint {
	PtVec3 PointOn0, PointOn1;       /* PointOn1 is on the plane owner */
	PtVec3 Normal;                   /* lies on Body1, points toward Body0 (ContactPoint.h:49-53 [V]) */
	double Phi;                      /* < 0 = penetrating */
	double NormalImpulse;            /* NaN if n/a */
	PtVec3 FrictionImpulse;          /* impulse on Body0; NaN if n/a */
	uint32_t Body0, Body1;           /* Body0 = the more mobile body; ties -> lower index */
	uint16_t Shape0, Shape1;
	uint32_t Flags;
} PtContactPoint;       /* 128 B */

typedef struct PtJointSnapshot { PtTransform Frame1World, Frame2World; PtVec3 LinearImpulse, AngularImpulse; uint32_t JointIndex, Flags; } PtJointSnapshot;  /* 168 B */

/* Native debug draw command type: mirrors IChaosDDRenderer::Render* (ChaosDDRenderer.h:19-44 [V]) */
#define PT_DD_POINT     0u
#define PT_DD_LINE      1u
#define PT_DD_ARROW     2u
#define PT_DD_CIRCLE    3u
#define PT_DD_SPHERE    4u
#define PT_DD_CAPSULE   5u
#define PT_DD_BOX       6u
#define PT_DD_TRIANGLE  7u
#define PT_DD_STRING    8u

typedef struct PtDebugDrawCmd {
	uint32_t Type, ColorBGRA;
	float Thickness /* cm; 0 = 1 px */, Size /* point px / arrow size / radius */;
	PtVec3 A, B, C;
	PtQuat Rotation;
	PtVec3 Extent;                   /* box: half extent (Chaos callers pass 0.5*Extents as RenderBox Size,
                                      DebugDrawImplicitObject.cpp:73-76 [V]);
                                      capsule: (Radius, HalfHeight incl. radius = 0.5*Height + Radius, 0)
                                      (DebugDrawImplicitObject.cpp:62 [V]) */
	float Lifetime;
	uint8_t DepthPriority, bDrawShadow; uint16_t Reserved;
	uint32_t TextOffset, TextLength;
} PtDebugDrawCmd;       /* 160 B */

typedef struct PtDebugSnapshotRequest { uint32_t StructSize, Flags, MaxContactPoints, MaxNativeCommands, NativeDrawCategories, Reserved; } PtDebugSnapshotRequest;
typedef struct PtDebugSnapshotCounts { uint32_t NumParticles, NumContactPoints, NumJoints, NumNativeCommands, NumStringBytes, bTruncated; } PtDebugSnapshotCounts;
typedef struct PtDebugSnapshotBuffers {
	PtParticleSnapshot* Particles;     uint32_t ParticleCapacity, ParticleStride;
	PtContactPoint*     ContactPoints; uint32_t ContactCapacity,  ContactStride;
	PtJointSnapshot*    Joints;        uint32_t JointCapacity,    JointStride;
	PtDebugDrawCmd*     NativeCommands; uint32_t CommandCapacity, CommandStride;
	char* StringBytes; uint32_t StringCapacity, Reserved;
} PtDebugSnapshotBuffers;

PT_STATIC_ASSERT(sizeof(PtPhysicalMaterial) == 48, "abi");
PT_STATIC_ASSERT(sizeof(PtShapeElem) == 136, "abi");
PT_STATIC_ASSERT(sizeof(PtActorCreateInfo) == 288, "abi");
PT_STATIC_ASSERT(sizeof(PtConstraintCreateInfo) == 144, "abi");
PT_STATIC_ASSERT(sizeof(PtBodyCommand) == 120, "abi");
PT_STATIC_ASSERT(sizeof(PtBodyState) == 112, "abi");
PT_STATIC_ASSERT(sizeof(PtMassProperties) == 88, "abi");
PT_STATIC_ASSERT(sizeof(PtStepStats) == 96, "abi");
PT_STATIC_ASSERT(sizeof(PtContactPoint) == 128, "abi");
PT_STATIC_ASSERT(sizeof(PtParticleSnapshot) == 224, "abi");
PT_STATIC_ASSERT(sizeof(PtJointSnapshot) == 168, "abi");
PT_STATIC_ASSERT(sizeof(PtDebugDrawCmd) == 160, "abi");

typedef struct PtBackendApi {
	uint32_t StructSize; uint32_t AbiVersion; const char* ModuleName;
	PtResult (PT_CALL *GetInfo)(PtBackendInfo* Out);
	PtResult (PT_CALL *InitGamePhys)(const PtInitParams* Params);
	PtResult (PT_CALL *TermGamePhys)(void);
	PtResult (PT_CALL *CreateScene)(const PtSceneDesc* Desc, PtSceneId* OutScene);
	PtResult (PT_CALL *DestroyScene)(PtSceneId Scene);
	PtResult (PT_CALL *CreateMaterials)(PtSceneId Scene, const PtPhysicalMaterial* Materials, uint32_t Num, uint32_t Stride);
	/* CreateActors, UE order (FInitBodiesHelper): per body CreateActor (creation-time flags) -> geometry, filter, material
     -> ONE AddActorsToScene_AssumesLocked (PhysX addActors) -> per body InitDynamicProperties in UE order
     -> if !PT_BODY_START_AWAKE and not kinematic: wake counter 0 + putToSleep (the body must already be in a scene);
        skipped in every engine when PT_SIM_ALLOW_SLEEPING is off (Normalized), the skipped count is recorded
     (BodyInstance.cpp:1669,1695-1706,4723-4733; NpRigidDynamic.cpp:521-527 [V]) */
	PtResult (PT_CALL *CreateActors)(PtSceneId Scene, const PtActorBatch* Batch);
	PtResult (PT_CALL *ReleaseActors)(PtSceneId Scene, const uint32_t* BodyIndices, uint32_t Num);
	PtResult (PT_CALL *CreateConstraints)(PtSceneId Scene, const PtConstraintCreateInfo* Infos, uint32_t Num, uint32_t Stride);  /* PROVISIONAL(M9) */
	PtResult (PT_CALL *PrepareForSimulation)(PtSceneId Scene, PtTextBuffer* OutActions);
	PtResult (PT_CALL *SetUpForFrame)(PtSceneId Scene, const PtVec3* NewGrav, float DeltaSeconds, float MinDeltaTime,
                                    float MaxDeltaTime, float MaxSubstepDeltaTime, int32_t MaxSubsteps, uint32_t bSubstepping);
	PtResult (PT_CALL *StartFrame)(PtSceneId Scene);
	PtResult (PT_CALL *WaitPhysScenes)(PtSceneId Scene);
	PtResult (PT_CALL *EndFrame)(PtSceneId Scene, PtFrameResult* OutResult);
	PtResult (PT_CALL *ApplyBodyCommands)(PtSceneId Scene, const PtBodyCommand* Cmds, uint32_t Num, uint32_t Stride);
	PtResult (PT_CALL *ReadBodyStates)(PtSceneId Scene, uint32_t ReadFlags, uint32_t* OutIndices, PtBodyState* OutStates,
                                     uint32_t Capacity, uint32_t* OutCount);
	PtResult (PT_CALL *ReadMassProperties)(PtSceneId Scene, uint32_t First, uint32_t Num, PtMassProperties* Out);
	PtResult (PT_CALL *GetStepStats)(PtSceneId Scene, PtStepStats* Out);
	PtResult (PT_CALL *CaptureDebugSnapshot)(PtSceneId Scene, const PtDebugSnapshotRequest* Req, PtDebugSnapshotCounts* Out);  /* PROVISIONAL(M9) */
	PtResult (PT_CALL *ReadDebugSnapshot)(PtSceneId Scene, const PtDebugSnapshotBuffers* Buffers);                             /* PROVISIONAL(M9) */
	PtResult (PT_CALL *ComputeNativeStateHash)(PtSceneId Scene, uint64_t* OutHash);
	PtResult (PT_CALL *SetOption)(PtSceneId SceneOrZero, const char* Key, const char* Value);
	PtResult (PT_CALL *DumpEffectiveSettings)(PtSceneId SceneOrZero, PtTextBuffer* OutJsonLines);
	PtResult (PT_CALL *GetMemoryStats)(PtSceneId SceneOrZero, PtMemoryStats* Out);
	PtResult (PT_CALL *GetLastErrorMessage)(PtTextBuffer* Out);
	/* v1.x: append new entries here only */
} PtBackendApi;
typedef const PtBackendApi* (PT_CALL *PtGetBackendApiFn)(uint32_t RequestedAbiMajor);   /* NULL for an unsupported major */
/* static backends: PT_GetBackendApi_Null / _Jolt / _Bullet / _PhysX;  DLL backends: PT_GetBackendApi */
#ifdef __cplusplus
}
#endif
#endif
