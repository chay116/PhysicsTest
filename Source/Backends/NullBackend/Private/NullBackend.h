// NullBackend.h - the SDK-free reference backend and the template for the Chaos bridge (02_Architecture 7.7, A.13).
// [MVP M2; the NATIVE_FRAME_MODEL gate A10 runs again as an M6 freeze condition]   PT_BACKEND_NS=Null
//
// What it is:
//   - double-precision semi-implicit Euler with UE / Chaos per-step order (FPBDRigidsEvolutionGBF::Integrate,
//     PBDRigidsEvolutionGBF.cpp:904-982 [V]): v += a dt, impulses, ether drag v *= max(0, 1 - c dt), velocity clamps,
//     x += v dt, q = normalize(q + dt/2 (w, 0) q) (Chaos TRotation::IntegrateRotationWithAngularVelocity,
//     ChaosCore Rotation.cpp:158-162 [V]). No collision, no automatic sleep, no gyroscopic term.
//   - PT_CAP_NATIVE_FRAME_MODEL: SetUpForFrame takes UE's FChaosScene::SetUpForFrame arguments once per frame and
//     FNullFramePlanner turns them into steps with UE 5.8.3's rules (Sync clamp, UE5 substepping, AsyncFixed accumulator,
//     MinDeltaTime drop). Null cannot link harness code (R4), so the planner is written independently of the harness
//     FStepPlanner; gate A10 compares the two (NumStepsTaken == FStepPlan::NumSteps).
//   - Commands follow FChaosEngineInterface / FChaosStateOps (ChaosEngineInterface.cpp:848-951 [V]) and UE5's frame
//     rules (PtBackendAbi.h body commands, docs/Abi.md 5.10): impulses and velocity changes accumulate and enter the
//     integration of the next step that runs; AddVelocity is an impulse of M * dv; AddForce / AddTorque wake a sleeping
//     body (any vector: the game-thread proxy wakes on every call, SingleParticlePhysicsProxy.h:706-771 [V]) unless
//     flagged PT_CMDF_NO_WAKE (the host's split of that wake, docs/Abi.md 5.10: then a sleeping body ignores them); a
//     teleport always wakes, and on a kinematic body it also becomes the kinematic target with zero velocity
//     (ChaosEngineInterface.cpp:2550-2557 [V]). Forces
//     accumulate until the next frame that pushes steps, are averaged over the frames since the last push and act on
//     every step of that frame (ChaosMarshallingManager.cpp:105-111, :325-342; PhysicsSolverBase.cpp:623-646;
//     PBDRigidsSolver.cpp:1582 [V]). PT_CMDF_ALLOW_SUBSTEPPING is ignored, as UE5 ignores bAllowSubstepping.
//     Kinematic targets follow EKinematicTargetMode::Position (PBDRigidsEvolutionGBF.cpp:1203-1220 [V]): reached at
//     the last step of a solver-substepped frame (Lerp / Slerp by 1 / (N - k) from the current pose), otherwise at the
//     first step. Deviation: Chaos interpolates the rotation with FRotation3f::Slerp in float, Null in double.
//
// The DLL build (NullBackendDll.cpp -> PtBackend_Null.dll) uses no C++ library code: arrays are malloc-based, so the
// DLL imports only the C runtime and KERNEL32 (Tools/check_modules.py, gate A9).
#pragma once

#include "PhysicsTestAbi/PtBackendKit.h"
#include "PtTypes.h"

namespace PhysicsTest::NullBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	// The engine steps of one frame. Every step of a frame has the same dt in UE's rules: a pushed step advances 0 or 1
	// solver step (PBDRigidsSolver.cpp:560-575 [V]: StepsRemaining is 1 when the solver substeps, MaxSubSteps is 1
	// otherwise).
	struct FNullStepPlan
	{
		uint32 NumSteps = 0;
		uint32 NumPushedSteps = 0;
		double StepDeltaTime = 0.0;    // FReal, as the UE solver advances (D / N in double when substepping)
		double FrameDeltaTime = 0.0;   // the InDt of AdvanceAndDispatch_External (MDeltaTime); > 0 counts as an external step
		bool bSolverSubstepped = false;   // MaxSubSteps > 1 (SetSolverSubstep_External): kinematic targets are interpolated
	};

	// Independent re-implementation of UE 5.8.3's frame-to-step arithmetic, in UE's types:
	//   FChaosScene::SetUpForFrame (ChaosScene.cpp:331-361 [V])                    float clamps
	//   FPhysicsSolverBase::AdvanceAndDispatch_External (PhysicsSolverBase.cpp:581-621 [V])  FReal pushed steps
	//   the FPBDRigidsSolver advance loop (PBDRigidsSolver.cpp:554-575 [V])        steps above MinDeltaTime only
	class FNullFramePlanner
	{
	public:
		// AsyncDt: the scene's PtSceneDesc::AsyncFixedTimeStepSize (-1 = sync). InOutAccumulatedTime: the fixed-dt
		// accumulator (Chaos AccumulatedTime), used only when AsyncDt > 0.
		static FNullStepPlan Plan(const ::PhysicsTest::Abi::FFrameParams& Params, double AsyncDt, double& InOutAccumulatedTime);
	};

	// Growable array of trivially copyable values on malloc / realloc (no C++ library, see above). Counts the bytes it
	// holds into an owner-provided memory counter.
	struct FNullMemoryCounter
	{
		int64 LiveBytes = 0;
		int64 PeakBytes = 0;
		int64 NumAllocs = 0;
		int64 NumFrees = 0;
	};

	template <class T>
	class TNullArray
	{
	public:
		TNullArray() = default;
		TNullArray(const TNullArray&) = delete;
		TNullArray& operator=(const TNullArray&) = delete;
		~TNullArray() { Reset(); }

		void SetCounter(FNullMemoryCounter* InCounter) { Counter = InCounter; }
		bool Reserve(uint32 NewCapacity);
		bool Add(const T& Value)
		{
			if (Count == Capacity && !Reserve(Capacity < 16 ? 16 : Capacity * 2))
			{
				return false;
			}
			Data[Count++] = Value;
			return true;
		}
		void Reset();
		uint32 Num() const { return Count; }
		T& operator[](const uint32 Index) { return Data[Index]; }
		const T& operator[](const uint32 Index) const { return Data[Index]; }

	private:
		T* Data = nullptr;
		uint32 Count = 0;
		uint32 Capacity = 0;
		FNullMemoryCounter* Counter = nullptr;
	};

	// One rigid body. The integrator works on the centre of mass (P, Q = COM position and principal-frame orientation),
	// like Chaos' XCom / RCom; the actor pose is derived through ComLocal. V is the COM velocity of a dynamic body and the
	// actor-origin velocity of a kinematic body (Chaos ApplyKinematicTargets, PtBodyState).
	struct FNullBody
	{
		PtTransform ComLocal = {};              // COM frame in actor space
		PtVec3 P = {};                          // COM position, cm
		PtQuat Q = {};                          // principal-frame orientation
		PtVec3 V = {};                          // cm/s
		PtVec3 W = {};                          // rad/s
		PtVec3 InertiaDiagonal = {};            // kg*cm^2, principal
		PtVec3 InvInertia = {};
		double Mass = 0.0;
		double InvMass = 0.0;
		double LinearDamping = 0.0;
		double AngularDamping = 0.0;
		double MaxAngularVelocity = 0.0;        // rad/s, <= 0 = unclamped
		double MaxLinearVelocity = 0.0;         // cm/s, <= 0 = unclamped
		PtVec3 LinearAccel = {};                // forces since the last push, as accelerations (Chaos GT Acceleration)
		PtVec3 AngularAccel = {};               // torques since the last push (Chaos GT AngularAcceleration)
		PtVec3 LinearImpulseVelocity = {};      // accumulated impulses as velocity changes (Chaos LinearImpulseVelocity)
		PtVec3 AngularImpulseVelocity = {};
		PtTransform KinematicTarget = {};
		uint32 Motion = PT_MOTION_STATIC;
		uint32 Flags = 0;
		uint32 ObjectState = PT_OBJECT_STATE_UNINITIALIZED;
		bool bHasKinematicTarget = false;
		bool bReleased = false;
		bool bDirty = true;                     // reported by the next PT_READ_ACTIVE_ONLY read
	};

	class FNullScene final : public ::PhysicsTest::Abi::IPhysicsBackendScene
	{
	public:
		explicit FNullScene(const PtSceneDesc& Desc);

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
		PtResult ComputeNativeStateHash(uint64_t& OutHash) override;
		PtResult SetOption(::PhysicsTest::Abi::FStringRef Key, ::PhysicsTest::Abi::FStringRef Value) override;
		PtResult DumpEffectiveSettings(::PhysicsTest::Abi::FTextWriter& Out) override;
		PtResult GetMemoryStats(PtMemoryStats& Out) override;
		// The phase-timer test double (M7 fix round 0): the steps of each frame are timed and reported as phase 0.
		void SetPhaseTimers(bool bEnabled) { bPhaseTimers = bEnabled; }

	private:
		void Step(uint32 StepIndex);
		PtBodyState MakeState(const FNullBody& Body) const;

		FNullMemoryCounter Memory;
		TNullArray<FNullBody> Bodies;
		TNullArray<PtPhysicalMaterial> Materials;
		PtVec3 Gravity = {};
		double NominalDeltaTime = 0.0;
		double AsyncDeltaTime = -1.0;
		double AccumulatedTime = 0.0;
		uint32 SimFlags = 0;
		uint32 MaxBodiesHint = 0;
		uint32 NumStartAsleepSkipped = 0;
		::PhysicsTest::Abi::FFrameParams FrameParams;
		FNullStepPlan FramePlan;
		uint32 ExternalSteps = 0;   // FPhysicsSolverBase::ExternalSteps: frames with dt > 0 since the last push
		uint32 StepsTakenThisFrame = 0;
		uint64 TotalSteps = 0;
		bool bPhaseTimers = false;
		int64 LastStepsNs = 0;      // phase 0 ("null.steps"): the wall time of the last frame's steps
	};

	// The module flags a Null build declares: 0 for the static Null; the DLL (NullBackendDll.cpp) declares
	// PT_MODULE_NO_REINIT when the process environment has PT_NULL_DLL_NO_REINIT=1 - the test double of a backend that
	// cannot be initialised twice (the Chaos bridge), for the bench's one-gate-per-process paths (M6 round 1).
	uint32 GetNullModuleFlags();
	// The phase-timer test double (M7 fix round 0, the bench's phases tier): the DLL declares PT_CAP_PHASE_TIMERS with one
	// timer ("null.steps") when the process environment has PT_NULL_DLL_PHASE_TIMERS=1, and times a scene's steps when the
	// scene asks for PT_INSTR_PHASES; the static Null never does. GetNullClockNs: QueryPerformanceCounter in ns (KERNEL32
	// only, gate A9; the static build returns 0).
	bool GetNullPhaseTimersEnabled();
	int64 GetNullClockNs();

	class FNullBackend final : public ::PhysicsTest::Abi::IPhysicsBackend
	{
	public:
		static constexpr const char* ModuleName = "Null";

		FNullBackend();
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
		uint32 ModuleFlags = 0;   // GetNullModuleFlags(), once at construction (the Kit reads GetInfo once, too)
		bool bPhaseTimers = false;   // GetNullPhaseTimersEnabled(), once at construction
	};
}
