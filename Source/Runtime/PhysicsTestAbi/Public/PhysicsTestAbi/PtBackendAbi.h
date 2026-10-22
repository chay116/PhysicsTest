/* PtBackendAbi.h - pure C11. The ONLY header shared with the Chaos DLL (02_Architecture 7, A.1, docs/Abi.md).
   Units: cm, kg, s, rad, Z-up, double.
   v1.0 (major 1): the CORE is FROZEN since M6 (tag abi-v1.0, docs/Abi.md 2). v1.x is append-only: fields after
   StructSize / Stride-guarded tails, entries at the end of PtBackendApi, new bits and enum values; anything else bumps the
   major and every DLL backend (Chaos) is rebuilt. v1.1 (M9, final; the lead tags abi-v1.1 on the M9 commit): joints, the
   debug snapshot and native draw - provisional from M6 to M9 - are final and frozen too (docs/Abi.md 9.2, 9.4): their
   layouts are the v1.0 ones, v1.1 fixed their meaning and appended one bit and two masks (PT_JOINTSNAP_ANGULAR_IMPULSE_NA;
   PT_JOINTSNAP_ALL, PT_SNAP_ALL), checked append-only against abi-v1.0 (every struct's size and offsets unchanged, a v1.1
   host runs a v1.0 module and a v1.0 host a v1.1 one).

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
     - batches are all-or-nothing: an invalid element rejects the whole call before anything changes (a backend must not
       fail a command it validated; if it does, the call returns PT_E_INTERNAL and the batch may be partially applied)
     - input structs carry StructSize; a value below this header's sizeof() is PT_E_INVALID_ARG (a larger one, from
       a newer host, is accepted and the unknown tail ignored)
     - every Reserved* field of an input struct is 0: the host writes 0 and a Kit-based module rejects anything else with
       PT_E_INVALID_ARG (M7, docs/Abi.md 2), so a v1.x meaning of a Reserved field is written only for a module that
       declares the capability giving it that meaning */
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

/* v1.0 since the M6 freeze (v0.9 = major 0 before it). The host always calls PT_GetBackendApi(PT_ABI_VERSION_MAJOR)
   with the major it was built with: a module built against another major (an M2.5 spike DLL of major 0) returns NULL
   and is refused; rebuild it against this header. */
#define PT_ABI_VERSION_MAJOR 1u
#define PT_ABI_VERSION_MINOR 1u                        /* v1.1 (M9): the M9 entries final, append-only to v1.0 */
#define PT_ABI_VERSION ((PT_ABI_VERSION_MAJOR << 16) | PT_ABI_VERSION_MINOR)
#define PT_ABI_PROVISIONAL_REVISION 3u                 /* FROZEN at 3 since v1.1 (M9). The revision of the formerly provisional
                                                          entries a module was built with (PtBackendInfo.ProvisionalRevision):
                                                          1 = M6-M8, 2 = M9 prep (joint semantics), 3 = v1.1 (the angular impulse
                                                          about the anchor, PT_JOINTSNAP_ANGULAR_IMPULSE_NA, the native draw
                                                          rules). Layouts unchanged since 1 (docs/Abi.md 9.4) */
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

/* ---- Joint type: v1.1 (M9; docs/Abi.md 9.4) ----
   Both are rigid (hard) joints solved by the engine's own joint solver: no limits, drives, softness, breakage or
   adapter-side projection (DL31; Phase 1). Frame1 / Frame2 are rigid poses in Body1's / Body2's actor space (the body
   origin, not the COM; unscaled; Frame2 in world space when Body2 is the world). Locked linear axes keep the two frame
   origins together. */
#define PT_JOINT_BALL   0u   /* ConstraintUtils::ConfigureAsBallAndSocket: linear locked, swing 1 / 2 and twist free */
#define PT_JOINT_HINGE  1u   /* ConstraintUtils::ConfigureAsHinge: linear locked, twist about Frame X free, both swings locked:
                                the X axes of the two frames stay parallel (same direction), rotation about them is free */
#define PT_JOINT_FIXED  2u   /* reserved: PT_E_INVALID_ARG */

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
#define PT_JOINTF_DISABLE_COLLISION  (1u << 0)   /* v1.1: FConstraintProfileProperties::bDisableCollision - every shape
                                                    of Body1 ignores every shape of Body2 (both directions; filters otherwise
                                                    unchanged). Clear (UE's default): the pair collides by its filters. No
                                                    effect for a world joint */
#define PT_JOINTF_ALL                0x1u        /* every defined PT_JOINTF_* bit; others -> PT_E_INVALID_ARG */
#define PT_READ_ALL          0u   /* dense: one state per created body, in index order. A released body reports ObjectState
                                     UNINITIALIZED with a zero pose (identity rotation) and zero velocities. OutIndices may
                                     be NULL; when it is not, the module writes OutIndices[i] = i for every state written
                                     (the Kit does it for every Kit backend, M3) */
#define PT_READ_ACTIVE_ONLY  1u   /* every live body whose state may have changed since the previous read: awake dynamic
                                     bodies, moved kinematic bodies and any body whose ObjectState changed. OutIndices
                                     is required (Capacity > 0); released bodies are never reported */

/* ---- Capabilities ---- */
#define PT_CAP(n) ((uint64_t)1 << (n))
#define PT_CAP_COLLISION                 PT_CAP(0)
#define PT_CAP_SHAPE_CONVEX              PT_CAP(1)
#define PT_CAP_SHAPE_TRIMESH_STATIC      PT_CAP(2)
#define PT_CAP_KINEMATIC                 PT_CAP(3)
#define PT_CAP_JOINT_BALL                PT_CAP(4)   /* v1.1: CreateConstraints accepts PT_JOINT_BALL. A module with neither
                                                        joint capability returns PT_E_UNSUPPORTED for every well-formed
                                                        CreateConstraints call (Num 0 included); a well-formed joint of a type
                                                        the module lacks is PT_E_UNSUPPORTED (argument errors come first) */
#define PT_CAP_JOINT_HINGE               PT_CAP(5)   /* v1.1: CreateConstraints accepts PT_JOINT_HINGE */
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
#define PT_CAP_CONTACT_POINTS            PT_CAP(16)  /* v1.1 (debug snapshot) */
#define PT_CAP_CONTACT_NORMAL_IMPULSE    PT_CAP(17)  /* v1.1 */
#define PT_CAP_CONTACT_FRICTION_IMPULSE  PT_CAP(18)  /* v1.1 */
#define PT_CAP_CONTACT_ACTIVE_EXACT      PT_CAP(19)  /* v1.1 */
#define PT_CAP_NATIVE_ISLANDS            PT_CAP(20)  /* v1.1. Jolt does not declare it: GetIslandIndexInternal is an
                                                        internal, per-step API (MotionProperties.h:258 [V]); the viewer uses
                                                        the harness FIslandBuilder */
#define PT_CAP_SLEEP_THRESHOLD_INFO      PT_CAP(21)  /* v1.1: reserved with PT_SNAP_SLEEP_INFO (per-body sleep thresholds and
                                                        counters). No module declares it and no snapshot buffer carries it:
                                                        PtDebugSnapshotBuffers has no StructSize, so a v1.x meaning comes with a
                                                        read entry appended to PtBackendApi (docs/Abi.md 9.2) */
#define PT_CAP_BOUNDS                    PT_CAP(22)  /* v1.1 */
#define PT_CAP_JOINT_SNAPSHOT            PT_CAP(23)  /* v1.1: PT_SNAP_JOINTS returns one PtJointSnapshot per live joint;
                                                        without it the snapshot holds no joints (the host draws joints from its
                                                        own pulled body states) */
#define PT_CAP_NATIVE_DEBUG_DRAW         PT_CAP(24)  /* v1.1: PT_SNAP_NATIVE_DRAW returns the engine's own debug draw of the last
                                                        step as PtDebugDrawCmd records. Viz runs only (PT_RUN_VIZ): PhysX sets its
                                                        visualization parameters, Bullet attaches its drawer, and a bench or
                                                        behaviour run draws nothing (a capture returns 0 commands). Bullet's
                                                        DBG_NoDeactivation mirrors the profile (btDiscreteDynamicsWorld.cpp:418-421
                                                        [V]); drawing must not change the result (G11 -G11Knob=viz) */
#define PT_CAP_PHASE_TIMERS              PT_CAP(25)
#define PT_CAP_ALLOC_STATS               PT_CAP(26)
/* The capabilities that were PROVISIONAL(M9) until v1.1: a host reads them as absent when PtBackendInfo.ProvisionalRevision
   differs from its PT_ABI_PROVISIONAL_REVISION (a module built before the v1.1 freeze). The name stays (append-only). */
#define PT_CAP_PROVISIONAL_MASK (PT_CAP_JOINT_BALL | PT_CAP_JOINT_HINGE | PT_CAP_CONTACT_POINTS | PT_CAP_CONTACT_NORMAL_IMPULSE | \
                                 PT_CAP_CONTACT_FRICTION_IMPULSE | PT_CAP_CONTACT_ACTIVE_EXACT | PT_CAP_NATIVE_ISLANDS | \
                                 PT_CAP_SLEEP_THRESHOLD_INFO | PT_CAP_BOUNDS | PT_CAP_JOINT_SNAPSHOT | PT_CAP_NATIVE_DEBUG_DRAW)

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
                                      treats every PT_CAP_PROVISIONAL_MASK capability as absent */
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
	double MaxDepenetrationVelocity; /* cm/s, UE 5.8.3 semantics (M3): < 0 = unset, the solver setting. FBodyInstance sends -1
                                      unless bOverrideMaxDepenetrationVelocity (BodyInstance.cpp:3713-3722 [V]); Chaos then
                                      uses SolverOptions.CollisionInitialOverlapDepenetrationVelocity, whose default -1 means
                                      unlimited (1e10 cm/s; PBDRigidsSolver.cpp:3668-3672, :3700, PBDCollisionConstraints.h:
                                      250-264 [V]). >= 0 = this body's rate; 0 = initially overlapping shapes are NOT pushed
                                      apart (PBDCollisionContainerSolver.cpp:180-219 [V]); a pair uses the larger of its two
                                      bodies' values (PBDCollisionConstraint.cpp:341-344 [V]). SDK backends: unset = the
                                      engine's own behaviour (recorded); PhysX UE4Compat maps it to UE4's project rule (M5).
                                      >= 0 on an engine without a per-body rate (Jolt): ignored, and the effective key
                                      (jolt.max_depenetration_velocity = n/a) records it */
	double SleepThresholdMultiplier, StabilizationThresholdMultiplier;
	uint64_t UserTag;
} PtActorCreateInfo;    /* 288 B */

typedef struct PtActorBatch {
	uint32_t StructSize;
	uint32_t NumActors;          const PtActorCreateInfo* Actors; uint32_t ActorStride;
	uint32_t NumShapes;          const PtShapeElem* Shapes;       uint32_t ShapeStride;
	uint32_t NumConvexVertices;  const PtVec3* ConvexVertices;
} PtActorBatch;

/* CreateConstraints (v1.1, M9; docs/Abi.md 9.4). Called in Idle after the actors exist (the host:
   FPhysScene::FlushDeferredActors, after CreateActors, 02 6). A joint's index is its creation order over every successful
   CreateConstraints call of the scene (0, 1, ...; never reused) - PtJointSnapshot::JointIndex. The batch is all or nothing.
   Kit checks (PT_E_INVALID_ARG unless noted): Stride >= sizeof and aligned; Type BALL or HINGE; Body1 a created, live body
   (never PT_INDEX_NONE); Body2 a created, live body other than Body1, or PT_INDEX_NONE (the world); unknown Flags bits;
   Reserved0 / Reserved1 not 0 (bit test); a frame not finite or its rotation not unit (1e-6); then PT_E_UNSUPPORTED for a
   type without its capability. Releasing a body (ReleaseActors) releases every joint attached to it: the indices stay,
   snapshots no longer report them. A joint between two bodies that are not dynamic is accepted and has no effect. */
typedef struct PtConstraintCreateInfo {   /* v1.1 */
	uint32_t Type;                   /* PT_JOINT_BALL / PT_JOINT_HINGE */
	uint32_t Body1;                  /* child (UE ConstraintBone1): a body index */
	uint32_t Body2;                  /* parent (UE ConstraintBone2): a body index, or PT_INDEX_NONE for the world */
	uint32_t Flags;                  /* PT_JOINTF_* */
	PtTransform Frame1, Frame2;      /* joint frames: Frame1 in Body1's actor space, Frame2 in Body2's (world space for the
	                                    world). Rigid (unit rotation, no scale). At rest the world frames coincide: origins
	                                    for both types, plus parallel X axes for HINGE; an initial violation is corrected by
	                                    the engine's solver like any other joint error */
	double Reserved0, Reserved1;     /* 0 (M9: were "TwistLimitLow / High, NaN"; limits are not part of Phase 1, DL31). A v1.x
	                                    meaning comes with a capability bit (docs/Abi.md 2 Reserved rule) */
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
#define PT_CMD_SET_LINEAR_VELOCITY      3u   /* Vector, cm/s. v1.0: wakes a sleeping dynamic body whatever PT_CMDF_AUTO_WAKE says
                                                (UE 5.8.3 SetV / SetW, SingleParticlePhysicsProxy.h:322-340, :1217-1229 [V]),
                                                the zero vector included. One exception is backend-defined: a vector that is
                                                not zero but nearly zero (every component <= 1e-4) - UE leaves the body asleep
                                                (SetVBase's !IsNearlyZero(), :331 [V]); Null and the SDK backends wake it. The
                                                backend declares which in the generic effective key
                                                command.nearly_zero_velocity_wakes (1 wakes, 0 stays asleep; G6 checks it) */
#define PT_CMD_SET_ANGULAR_VELOCITY     4u   /* Vector, rad/s; wakes like SET_LINEAR_VELOCITY */
#define PT_CMD_ADD_IMPULSE              5u   /* AddImpulse_AssumesLocked: kg*cm/s */
#define PT_CMD_ADD_ANGULAR_IMPULSE      6u   /* AddAngularImpulseInRadians_AssumesLocked */
#define PT_CMD_ADD_FORCE                7u   /* AddForce_AssumesLocked (PT_CMDF_ACCEL_CHANGE, PT_CMDF_ALLOW_SUBSTEPPING, PT_CMDF_NO_WAKE) */
#define PT_CMD_ADD_TORQUE               8u   /* AddTorque_AssumesLocked */
#define PT_CMD_WAKE_UP                  9u
#define PT_CMD_PUT_TO_SLEEP            10u
#define PT_CMD_SET_BODY_STATE          11u   /* harness-only full state write (PT_CAP_STATE_WRITE): Transform = pose,
                                                Vector = linear, Vector2 = angular velocity. v1.0: wakes a sleeping dynamic
                                                body (a teleport plus velocities, as UE's SetX / SetV) */
#define PT_CMD_ADD_VELOCITY            12u   /* AddVelocity_AssumesLocked: cm/s, mass-independent */
#define PT_CMD_ADD_ANGULAR_VELOCITY    13u   /* AddAngularVelocityInRadians_AssumesLocked */
#define PT_CMDF_ALLOW_SUBSTEPPING  (1u << 0)  /* forces / torques: informational only. UE5 ignores bAllowSubstepping
                                                (ChaosEngineInterface.cpp:932-950, :1024-1037 [V]) and so does every
                                                NATIVE_FRAME_MODEL backend; the host uses it only for the UE4
                                                FPhysSubstepTask rule of the Harness driver (UE4PhysSubstep) */
#define PT_CMDF_ACCEL_CHANGE       (1u << 1)
#define PT_CMDF_AUTO_WAKE          (1u << 2)  /* informational in v1.0 (UE's bAutoWake): every pose and velocity write wakes a
                                                sleeping dynamic body - UE5's teleport (SetX, SingleParticlePhysicsProxy.h:
                                                240-251 [V]) and velocity writes (SetV / SetW, :322-340, :362-380 [V]; the
                                                nearly-zero velocity is backend-defined, see SET_LINEAR_VELOCITY); the
                                                engine interface supports only bAutoWake = true (ensure,
                                                ChaosEngineInterface.cpp:626-658 [V]) */
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
	PtVec3 LinearVelocity;           /* cm/s. Dynamic: of the COM. Kinematic: of the actor ORIGIN, as Chaos computes it from
                                      the particle X (ApplyKinematicTargets, PBDRigidsEvolutionGBF.cpp:1180-1239 [V]); the two
                                      differ by w x (R * COM) for a rotating kinematic body with a COM offset (M3) */
	PtVec3 AngularVelocity;          /* rad/s */
	uint32_t ObjectState, Flags;
} PtBodyState;          /* 112 B */

/* ReadMassProperties: what the engine has. The principal frame (CenterOfMassLocal.Rotation) and the order of the
   principal moments are the engine's (Jolt sorts them largest first), so compare R diag(I) R^T, not the diagonals. */
typedef struct PtMassProperties { double Mass; PtVec3 InertiaDiagonal; PtTransform CenterOfMassLocal; } PtMassProperties;  /* 88 B */
/* EndFrame's result (v1.0: 16 B; there is no StructSize, so the reserved word is the only room v1.x has). EndFrame writes
   all four fields; the host and the Kit pre-fill NumPushedSteps = PT_INDEX_NONE before the call.
   NumStepsTaken   the engine steps actually advanced this frame (= FStepPlan::NumSteps; the host stops on a mismatch)
   EngineErrorBits backend-defined engine error bits; the backend's capacity bits are named by the effective key
                   errors.capacity_bits (docs/Abi.md 5.14). 0 = none
   NumPushedSteps  a NATIVE_FRAME_MODEL backend: the steps pushed to the solver this frame (UE AdvanceAndDispatch_External's
                   NumSteps = FStepPlan::NumPushedSteps; AsyncBlockMode or MinDeltaTime can make it differ from
                   NumStepsTaken). PT_INDEX_NONE = not reported (the Kit pre-fills it; SDK backends run one step per frame)
   Reserved        0 */
typedef struct PtFrameResult { uint32_t NumStepsTaken; uint32_t EngineErrorBits; uint32_t NumPushedSteps; uint32_t Reserved; } PtFrameResult;
typedef struct PtStepStats {
	uint32_t StructSize;
	int32_t NumActiveBodies, NumSleepingBodies, NumContactPairs, NumContactPoints, NumIslands;   /* -1 = n/a */
	uint32_t EngineErrorBits, NumPhaseTimes;
	int64_t PhaseTimeNs[8];
} PtStepStats;          /* 96 B; reads that need no listeners are allowed in headline runs */
/* What the backend knows of its own memory (bytes; -1 = the backend cannot tell, e.g. allocation counts without an
   allocator hook; the Kit pre-fills the four counters with -1, so a field a backend does not set reads -1, not 0).
   AllocatorKind: 0 CRT malloc, 2 an engine temp / arena allocator (Jolt: the per-scene temp block). */
typedef struct PtMemoryStats { int64_t LiveBytes, PeakBytes, NumAllocs, NumFrees; uint32_t AllocatorKind, Reserved; } PtMemoryStats;

/* ---- Debug snapshot (Chaos conventions): v1.1 (M9; docs/Abi.md 9.1, 9.2) ---- */
#define PT_SNAP_PARTICLES       (1u << 0)
#define PT_SNAP_BOUNDS          (1u << 1)
#define PT_SNAP_CONTACTS        (1u << 2)
#define PT_SNAP_JOINTS          (1u << 3)
#define PT_SNAP_NATIVE_ISLANDS  (1u << 4)
#define PT_SNAP_SLEEP_INFO      (1u << 5)
#define PT_SNAP_NATIVE_DRAW     (1u << 6)
#define PT_SNAP_ALL             0x7Fu       /* v1.1: every defined PT_SNAP_* bit (PT_SNAP_SLEEP_INFO is defined and reserved: a module
                                               without PT_CAP_SLEEP_THRESHOLD_INFO ignores it). A Kit-based module refuses a request
                                               with any other bit, or a Reserved field of PtDebugSnapshotRequest / PtDebugSnapshotBuffers
                                               that is not 0, with PT_E_INVALID_ARG (docs/Abi.md 2 Reserved rule; M9 fix round 1) */
/* v1.0 flag rule (docs/Abi.md 9.1 decision 2, checked by G3): at most one of ACTIVE / ACTIVE_APPROX / SPECULATIVE.
   ACTIVE = the solver acted on the point in its last step - a normal impulse, or a push-out (Chaos bIsActive: NetPushOut or
   NetImpulse not nearly zero, ChaosDebugDraw.cpp:1453 [V]) - on PT_CAP_CONTACT_ACTIVE_EXACT backends; ACTIVE_APPROX = Phi
   <= 0 on a backend without exact solver results; SPECULATIVE = Phi > 0 and neither of the other two. A command that wakes
   a body of a sleeping pair drops that pair's kept (SLEEPING) points until the next step.
   ACTIVE and NormalImpulse (PT_CAP_CONTACT_NORMAL_IMPULSE) are tied one way only (M6 round 2): NormalImpulse > eps implies
   ACTIVE, ACTIVE implies NormalImpulse >= -eps (G3, tolerances.json G3.normal_impulse_eps). ACTIVE may carry a zero normal
   impulse (Chaos: friction only, or a push-out whose velocity the solve cancelled, PBDCollisionSolver.h:1178-1196 [V]).
   Bullet limit: ACTIVE = getAppliedImpulse() > 0; the split-impulse push (penetration beyond
   SplitImpulsePenetrationThreshold) is not written back to the manifold point, so a point corrected only by that push has
   neither ACTIVE nor an impulse (docs/Abi.md 7.1). */
#define PT_CONTACT_ACTIVE           (1u << 0)
#define PT_CONTACT_ACTIVE_APPROX    (1u << 1)   /* Jolt: phi <= 0 */
#define PT_CONTACT_SPECULATIVE      (1u << 2)
#define PT_CONTACT_STATIC_FRICTION  (1u << 3)
#define PT_CONTACT_SWAPPED          (1u << 4)
#define PT_CONTACT_SLEEPING         (1u << 5)   /* M4: a contact of a sleeping island kept from the step it fell asleep in,
                                                   with that step's other flags (Jolt reports no contacts for sleeping
                                                   bodies; Chaos keeps them and their last solver results) */

typedef struct PtParticleSnapshot {
	PtTransform ActorTransform, CenterOfMassWorld;
	PtVec3 BoundsMin, BoundsMax;     /* engine-defined bounds */
	PtVec3 LinearVelocity, AngularVelocity;
	uint32_t BodyIndex; uint8_t ObjectState, Flags; uint16_t Reserved;
	int32_t NativeIslandId; uint32_t Reserved2;
} PtParticleSnapshot;   /* 224 B */

typedef struct PtContactPoint {
	PtVec3 PointOn0, PointOn1;       /* PointOn0 on Body0's surface, PointOn1 on Body1's (the plane owner), at the pose the
	                                    step's narrow phase used (the start-of-step pose for Jolt, Bullet and PhysX; Chaos'
	                                    predicted pose P, Q after Integrate); (PointOn0 - PointOn1) . Normal = Phi (v1.0,
	                                    docs/Abi.md 7.1, 9.1 decision 3) */
	PtVec3 Normal;                   /* lies on Body1, points toward Body0 (ContactPoint.h:49-53 [V]) */
	double Phi;                      /* < 0 = penetrating */
	double NormalImpulse;            /* kg cm/s along Normal, on Body0, from the solver's last step, position corrections
	                                    included as impulses (Chaos: (NetImpulse + NetPushOut / Dt) . n, >= 0 by the solver's
	                                    clamp); NaN if n/a (no PT_CAP_CONTACT_NORMAL_IMPULSE). Bullet: getAppliedImpulse(),
	                                    without the split-impulse push (see the flag rule) */
	PtVec3 FrictionImpulse;          /* impulse on Body0; NaN if n/a */
	uint32_t Body0, Body1;           /* Body0 = the more mobile body; ties -> lower index */
	uint16_t Shape0, Shape1;
	uint32_t Flags;
} PtContactPoint;       /* 128 B */

/* One live joint (PT_SNAP_JOINTS on a PT_CAP_JOINT_SNAPSHOT backend), in JointIndex order (v1.1, docs/Abi.md 9.4 item 9).
   Chaos draws "a" = particle[1] / "b" = particle[0]: the harness maps Body1 to particle[0] and Body2 to particle[1].
   Frame1World / Frame2World  the joint frames in world space at the snapshot pose (the end of the last step): Body1's
                              actor pose x Frame1, Body2's x Frame2 (Frame2 itself for the world). The stretch is
                              |Frame1World.Translation - Frame2World.Translation|; for HINGE the angle between the two X
                              axes is the swing error
   LinearImpulse              what the joint applied to Body1 (the child) in the last step, world space, kg cm/s (an engine
                              that reports a force multiplies by its step dt); Body2 got the opposite
   AngularImpulse             the joint's angular impulse on Body1 in the last step about the joint anchor, Frame1World's
                              origin, world space, kg cm^2/s: the couple of the angular rows (zero for BALL), since the linear
                              impulse acts at the anchor - NOT about Body1's centre of mass (that would add
                              (anchor - COM) x LinearImpulse). Chaos' joint angular impulse is this couple. Body2 got
                              -LinearImpulse at its own anchor (Frame2World's origin) and the opposite couple about it; the two
                              anchors coincide while the joint holds
   both impulses              0 for a joint that did not run (no awake dynamic body: PT_JOINTSNAP_SLEEPING) or before its
                              first step; NaN where the engine cannot tell (PT_JOINTSNAP_IMPULSE_NA: both,
                              PT_JOINTSNAP_ANGULAR_IMPULSE_NA: the angular one only)
   JointIndex                 creation order (PtConstraintCreateInfo)
   Flags                      PT_JOINTSNAP_* */
typedef struct PtJointSnapshot { PtTransform Frame1World, Frame2World; PtVec3 LinearImpulse, AngularImpulse; uint32_t JointIndex, Flags; } PtJointSnapshot;  /* 168 B */
#define PT_JOINTSNAP_SLEEPING            (1u << 0)   /* no dynamic body of the joint is awake (kept from the step it fell asleep in;
                                                        also a joint between two bodies that are not dynamic): zero impulses */
#define PT_JOINTSNAP_IMPULSE_NA          (1u << 1)   /* LinearImpulse and AngularImpulse are NaN: the engine cannot report them */
#define PT_JOINTSNAP_ANGULAR_IMPULSE_NA  (1u << 2)   /* v1.1: AngularImpulse only is NaN, LinearImpulse is valid (PhysX 5.11 TGS: its
                                                        joint writeback carries no usable torque, docs/Abi.md 9.4) */
#define PT_JOINTSNAP_ALL                 0x7u        /* every defined PT_JOINTSNAP_* bit */

/* Native debug draw (v1.1, docs/Abi.md 9.2; PT_CAP_NATIVE_DEBUG_DRAW, PT_SNAP_NATIVE_DRAW). The engine's own drawing of the last
   step, in harness units (cm). Command type: mirrors IChaosDDRenderer::Render* (ChaosDDRenderer.h:19-44 [V]).
     ColorBGRA      the FColor memory layout B, G, R, A read as one little-endian uint32, i.e. the packed value 0xAARRGGBB
                    (FColor::FromPackedARGB; PhysX's PxDebugColor values are the same numbers). A primitive whose engine gives
                    each vertex its own colour carries the first vertex's colour
     pose           the pose the engine drew at - engine-defined: PhysX draws shapes, actor axes and joint frames at the pose
                    the step started from (one step behind the snapshot's particles), contacts at the end of the step;
                    Bullet's drawer runs at the capture (end of step)
     strings        TextOffset / TextLength index PtDebugDrawCmd text in the snapshot's string pool (not NUL-terminated)
     truncation     at most PtDebugSnapshotRequest.MaxNativeCommands commands, the first ones in the engine's order, and
                    PtDebugSnapshotCounts.bTruncated
   PtDebugSnapshotRequest.NativeDrawCategories is backend-defined: 0 = the backend's default set (the variant's categories);
   PhysX and Bullet define no categories and ignore it (PhysX draws inside simulate, so a capture cannot choose).
   Fields per type (v1.1 meaning, M9 fix round 2; the host decoder PhysicsDebugDraw ToLatentCommand reads exactly these; every
   field a type does not use is 0, Rotation then (0, 0, 0, 1); all lengths cm):
     POINT     A = position, Size = point size (px)
     LINE      A -> B
     ARROW     A -> B (tip), Size = arrow head size (cm, UE's ArrowSize)
     CIRCLE    A = centre, Size = radius; the circle lies in the plane of Rotation's Y and Z axes (UE's DrawDebugCircle with a
               matrix: IChaosDDRenderer::RenderCircle(Center, Axes, ...) passes Rotation = FQuat of the orthonormal Axes, so
               Rotation.GetAxisY() / GetAxisZ() = Axes.GetUnitAxis(EAxis::Y / Z); X is the circle's normal)
     SPHERE    A = centre, Size = radius
     CAPSULE   A = centre, Rotation (the capsule axis is Rotation's Z), Extent = (Radius, HalfHeight incl. radius, 0)
     BOX       A = centre, Rotation, Extent = half extent
     TRIANGLE  A, B, C
     STRING    A = location, TextOffset / TextLength, Size = font scale (0 reads as 1), bDrawShadow = 0 / 1
     every type ColorBGRA; Thickness (cm, 0 = 1 px) for the line-drawn types (not POINT); DepthPriority = UE's SDPG (0 world,
     1 foreground); Lifetime (s) = 0: drawn for this capture only - every repository engine writes 0, the host redraws the latest
     capture each frame and does not keep commands (a value > 0 is passed on as the command's lifetime, the duration for STRING);
     Reserved = 0. */
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
PT_STATIC_ASSERT(sizeof(PtFrameResult) == 16, "abi");
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
	PtResult (PT_CALL *CreateConstraints)(PtSceneId Scene, const PtConstraintCreateInfo* Infos, uint32_t Num, uint32_t Stride);  /* v1.1 meaning */
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
	PtResult (PT_CALL *CaptureDebugSnapshot)(PtSceneId Scene, const PtDebugSnapshotRequest* Req, PtDebugSnapshotCounts* Out);  /* v1.1 meaning */
	PtResult (PT_CALL *ReadDebugSnapshot)(PtSceneId Scene, const PtDebugSnapshotBuffers* Buffers);                             /* v1.1 meaning */
	PtResult (PT_CALL *ComputeNativeStateHash)(PtSceneId Scene, uint64_t* OutHash);
	PtResult (PT_CALL *SetOption)(PtSceneId SceneOrZero, const char* Key, const char* Value);
	PtResult (PT_CALL *DumpEffectiveSettings)(PtSceneId SceneOrZero, PtTextBuffer* OutJsonLines);
	PtResult (PT_CALL *GetMemoryStats)(PtSceneId SceneOrZero, PtMemoryStats* Out);
	PtResult (PT_CALL *GetLastErrorMessage)(PtTextBuffer* Out);
	/* v1.x: append new entries here only */
} PtBackendApi;
/* The v1.0 table (25 entries, 216 B) is the frozen minimum: a host accepts any table with StructSize >= PT_ABI_V1_0_API_SIZE
   and the host's major, so a v1.0 module (the Chaos bridge) still loads after a v1.x host appends entries. An entry
   beyond the module's StructSize is absent (NULL): a host reads an appended entry only through PT_API_HAS(Api, Entry), and
   only the 25 v1.0 entries must be non-NULL (docs/Abi.md 2). */
#define PT_ABI_V1_0_API_SIZE ((uint32_t)(offsetof(PtBackendApi, GetLastErrorMessage) + sizeof(void*)))
#define PT_API_HAS(Api, Entry) ((Api)->StructSize >= offsetof(PtBackendApi, Entry) + sizeof((Api)->Entry) && (Api)->Entry != NULL)
PT_STATIC_ASSERT(PT_ABI_V1_0_API_SIZE == 216, "abi: the v1.0 table size is frozen");
PT_STATIC_ASSERT(sizeof(PtBackendApi) >= PT_ABI_V1_0_API_SIZE, "abi: v1.x only appends entries");
typedef const PtBackendApi* (PT_CALL *PtGetBackendApiFn)(uint32_t RequestedAbiMajor);   /* NULL for an unsupported major */
/* static backends: PT_GetBackendApi_Null / _Jolt / _Bullet / _PhysX;  DLL backends: PT_GetBackendApi */
#ifdef __cplusplus
}
#endif
#endif
