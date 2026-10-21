// AbiGateTests.cpp - the M2 ABI gates on the Null backend through both paths (03_WorkPlan M2, 02_Architecture 14.2):
// the static backend linked into this exe ("Null") and PtBackend_Null.dll loaded with LoadLibraryExW ("NullDll").
// Also: Kit state isolation between two static backends (Null + the AbiTwin test backend), the Kit guards (MXCSR,
// exceptions, log callback), the Kit thread id, and the ABI call cost (recorded, not a gate).
// This TU is compiled with PT_BACKEND_NS=AutomationTest (the target's definition) so it may include the Kit.
#include "CoreMinimal.h"

#include "Abi/AbiGates.h"
#include "NullBackend/NullBackendEntry.h"
#include "Physics/PhysicsBackend.h"
#include "PhysicsTestAbi/PtBackendKit.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

extern "C" const PtBackendApi* PT_CALL PT_GetBackendApi_AbiTwin(uint32_t RequestedAbiMajor);

using namespace PhysicsTest;

namespace
{
	const char* const GAbiTargets[] = { "Null", "NullDll" };

	template <class FRun>
	void RunGateOnBothPaths(FRun&& Run)
	{
		for (const char* Name : GAbiTargets)
		{
			AbiGates::FAbiGateTarget Target;
			std::string Error;
			CAPTURE(Name);
			REQUIRE_MESSAGE(AbiGates::PrepareAbiGateTarget(Name, Target, Error), Error);
			const FGateReport Report = Run(Target);
			MESSAGE(Report.Summary());
			CHECK_MESSAGE(Report.Passed(), Report.Summary());
		}
	}

	std::string LastErrorOf(const PtBackendApi& Api)
	{
		char Text[1100] = {};
		PtTextBuffer Buffer = { Text, sizeof(Text), 0 };
		Api.GetLastErrorMessage(&Buffer);
		return Text;
	}

	PtInitParams MakeInit()
	{
		PtInitParams Init = {};
		Init.StructSize = sizeof(PtInitParams);
		Init.MaxThreads = 1;
		return Init;
	}

	PtSceneDesc MakeDesc()
	{
		PtSceneDesc Desc = {};
		Desc.StructSize = sizeof(PtSceneDesc);
		Desc.Gravity = PtVec3{ 0.0, 0.0, -980.0 };
		Desc.NominalDeltaTime = 1.0 / 60.0;
		Desc.AsyncFixedTimeStepSize = PT_ASYNC_DT_SYNC;
		Desc.NumThreads = 1;
		return Desc;
	}

	struct FCapturedLog
	{
		std::vector<std::string> Lines;
	};

	void PT_CALL CaptureLog(void* UserData, const uint32_t Level, const char* Category, const char* Message)
	{
		static_cast<FCapturedLog*>(UserData)->Lines.push_back(std::to_string(Level) + " " + Category + ": " + Message);
	}
}

TEST_CASE("PhysicsTest.Abi.A0Lifecycle")
{
	RunGateOnBothPaths([](const AbiGates::FAbiGateTarget& Target) { return AbiGates::RunA0Lifecycle(Target); });
}

TEST_CASE("PhysicsTest.Abi.A1VersionHandshake")
{
	RunGateOnBothPaths([](const AbiGates::FAbiGateTarget& Target) { return AbiGates::RunA1VersionHandshake(Target); });
}

TEST_CASE("PhysicsTest.Abi.A2OtherThread")
{
	RunGateOnBothPaths([](const AbiGates::FAbiGateTarget& Target) { return AbiGates::RunA2OtherThread(Target); });
}

TEST_CASE("PhysicsTest.Abi.A3PhaseStateMachine")
{
	RunGateOnBothPaths([](const AbiGates::FAbiGateTarget& Target) { return AbiGates::RunA3PhaseStateMachine(Target); });
}

TEST_CASE("PhysicsTest.Abi.A4NegativeCases")
{
	RunGateOnBothPaths([](const AbiGates::FAbiGateTarget& Target) { return AbiGates::RunA4NegativeCases(Target); });
}

TEST_CASE("PhysicsTest.Abi.A5CapacityAndStride")
{
	RunGateOnBothPaths([](const AbiGates::FAbiGateTarget& Target) { return AbiGates::RunA5CapacityAndStride(Target); });
}

TEST_CASE("PhysicsTest.Abi.A6FloatingPointState")
{
	RunGateOnBothPaths([](const AbiGates::FAbiGateTarget& Target) { return AbiGates::RunA6FloatingPointState(Target); });
}

TEST_CASE("PhysicsTest.Abi.A7EffectiveKeys")
{
	RunGateOnBothPaths([](const AbiGates::FAbiGateTarget& Target) { return AbiGates::RunA7EffectiveKeys(Target); });
}

TEST_CASE("PhysicsTest.Abi.A10FrameModel")
{
	for (const char* Name : GAbiTargets)
	{
		CAPTURE(Name);
		const FGateReport Report = AbiGates::RunA10FrameModel(Name);
		MESSAGE(Report.Summary());
		CHECK_MESSAGE(Report.Passed(), Report.Summary());
	}
}

TEST_CASE("PhysicsTest.Abi.StaticAndDllAreSeparateModules")
{
	// The static Null backend and PtBackend_Null.dll are two module instances: separate tables, separate Kit state.
	AbiGates::FAbiGateTarget Static;
	AbiGates::FAbiGateTarget Dll;
	std::string Error;
	REQUIRE_MESSAGE(AbiGates::PrepareAbiGateTarget("Null", Static, Error), Error);
	REQUIRE_MESSAGE(AbiGates::PrepareAbiGateTarget("NullDll", Dll, Error), Error);
	CHECK(Static.Api != Dll.Api);
	CHECK(Static.Entry == &PT_GetBackendApi_Null);
	CHECK(Dll.Module->IsDll());
	CHECK(Dll.Module->GetDllPath().filename() == L"PtBackend_Null.dll");
	CHECK(GetModuleHandleW(L"PtBackend_Null.dll") != nullptr);
	const PtInitParams Init = MakeInit();
	REQUIRE(Static.Api->InitGamePhys(&Init) == PT_OK);
	CHECK(Dll.Api->CreateScene(nullptr, nullptr) == PT_E_BAD_STATE);   // the DLL module is still not initialised
	CHECK(Static.Api->TermGamePhys() == PT_OK);
}

TEST_CASE("PhysicsTest.Abi.KitStateIsPerBackend")
{
	// 03 M2: Null and a second static test backend in one exe keep separate last-error, owner-thread and module state
	// (all Kit state is a static member of TBackendApiProvider<TBackend>; the linker map check is the CTest
	// PhysicsTest.Probe.KitNamespaces).
	AbiGates::FAbiGateTarget Null;
	std::string Error;
	REQUIRE_MESSAGE(AbiGates::PrepareAbiGateTarget("Null", Null, Error), Error);
	const PtBackendApi* Twin = PT_GetBackendApi_AbiTwin(PT_ABI_VERSION_MAJOR);
	REQUIRE(Twin != nullptr);
	CHECK(PT_GetBackendApi_AbiTwin(PT_ABI_VERSION_MAJOR + 1) == nullptr);
	CHECK(std::string(Twin->ModuleName) == "AbiTwin");
	const PtInitParams Init = MakeInit();
	REQUIRE(Null.Api->InitGamePhys(&Init) == PT_OK);
	CHECK(Twin->CreateScene(nullptr, nullptr) == PT_E_BAD_STATE);   // initialising Null did not initialise the twin
	REQUIRE(Twin->InitGamePhys(&Init) == PT_OK);

	CHECK(Null.Api->SetOption(0, "kit.state.null.option", "1") == PT_E_INVALID_ARG);
	CHECK(Twin->SetOption(0, "twin.fail", "the twin's own error") == PT_E_ENGINE);
	CHECK(LastErrorOf(*Null.Api).find("kit.state.null.option") != std::string::npos);
	CHECK(LastErrorOf(*Twin) == "the twin's own error");

	// Owner threads are per backend too: the twin re-initialised on a worker thread rejects this thread, Null does not.
	CHECK(Twin->TermGamePhys() == PT_OK);
	std::atomic<int32> Step{ 0 };
	PtResult OtherInit = PT_E_INTERNAL;
	PtResult OtherTerm = PT_E_INTERNAL;
	std::thread Worker([&]
	{
		OtherInit = Twin->InitGamePhys(&Init);
		Step.store(1);
		while (Step.load() != 2)
		{
			std::this_thread::yield();
		}
		OtherTerm = Twin->TermGamePhys();   // the owner thread terminates it
	});
	while (Step.load() != 1)
	{
		std::this_thread::yield();
	}
	CHECK(OtherInit == PT_OK);
	CHECK(Twin->SetOption(0, "twin.fail", "x") == PT_E_WRONG_THREAD);
	CHECK(Twin->TermGamePhys() == PT_E_WRONG_THREAD);
	PtSceneId Scene = 0;
	const PtSceneDesc Desc = MakeDesc();
	CHECK(Null.Api->CreateScene(&Desc, &Scene) == PT_OK);
	CHECK(Null.Api->DestroyScene(Scene) == PT_OK);
	Step.store(2);
	Worker.join();
	CHECK(OtherTerm == PT_OK);
	CHECK(Null.Api->TermGamePhys() == PT_OK);
}

TEST_CASE("PhysicsTest.Abi.KitGuards")
{
	// The Kit restores MXCSR control bits a backend changed (and warns once through the log callback), and turns a C++
	// exception into PT_E_INTERNAL (CMake builds; UE builds compile without exceptions, PtAbiUeCompatCheck).
	const PtBackendApi* Twin = PT_GetBackendApi_AbiTwin(PT_ABI_VERSION_MAJOR);
	REQUIRE(Twin != nullptr);
	FCapturedLog Log;
	PtInitParams Init = MakeInit();
	Init.LogCallback = &CaptureLog;
	Init.LogUserData = &Log;
	Twin->TermGamePhys();
	REQUIRE(Twin->InitGamePhys(&Init) == PT_OK);
	const unsigned int Before = _mm_getcsr() & 0xFFC0u;
	CHECK(Twin->SetOption(0, "twin.clobber_mxcsr", "1") == PT_OK);
	CHECK((_mm_getcsr() & 0xFFC0u) == Before);
	CHECK(Twin->SetOption(0, "twin.clobber_mxcsr", "1") == PT_OK);
	CHECK((_mm_getcsr() & 0xFFC0u) == Before);
	int32 Warnings = 0;
	for (const std::string& Line : Log.Lines)
	{
		Warnings += Line.find("MXCSR") != std::string::npos ? 1 : 0;
	}
	CHECK(Warnings == 1);   // logged on the first restore only
	// Every restore is counted in the module-level effective setting kit.mxcsr_restores (gate A6 reads it).
	char Settings[1024] = {};
	PtTextBuffer SettingsText = { Settings, sizeof(Settings), 0 };
	REQUIRE(Twin->DumpEffectiveSettings(0, &SettingsText) == PT_OK);
	CHECK(std::string(Settings).find("{\"key\":\"kit.mxcsr_restores\",\"native\":\"2\"") != std::string::npos);
	CHECK(Twin->SetOption(0, "twin.throw", "1") == PT_E_INTERNAL);
	CHECK(LastErrorOf(*Twin).find("C++ exception") != std::string::npos);
	CHECK(Twin->TermGamePhys() == PT_OK);
}

TEST_CASE("PhysicsTest.Abi.KitThreadIdMatchesWin32")
{
	// 02 14.1: the Kit's TEB thread id (__readgsdword(0x48)) equals GetCurrentThreadId(), on this and on another thread.
	CHECK(Abi::GetKitCurrentThreadId() == static_cast<uint32_t>(GetCurrentThreadId()));
	bool bOtherMatches = false;
	std::thread([&bOtherMatches] { bOtherMatches = Abi::GetKitCurrentThreadId() == static_cast<uint32_t>(GetCurrentThreadId()); }).join();
	CHECK(bOtherMatches);
}

TEST_CASE("PhysicsTest.Abi.HostRefusesBadModules")
{
	// A DLL that is not there, or a static entry that returns NULL for the host's major, is refused with a reason.
	std::string Error;
	CHECK(FPhysicsBackendModule::LoadDll("Missing", FPhysicsBackendRegistry::GetExecutableDirectory() / L"PtBackend_Missing.dll", Error) == nullptr);
	CHECK(Error.find("not found") != std::string::npos);
	const PtGetBackendApiFn NoMajor = [](uint32_t) -> const PtBackendApi* { return nullptr; };
	CHECK(FPhysicsBackendModule::CreateStatic("NoMajor", NoMajor, Error) == nullptr);
	CHECK(Error.find("does not support this ABI major") != std::string::npos);
	CHECK(FPhysicsBackendRegistry::Get().FindOrLoadModule("NoSuchBackend", Error) == nullptr);
	std::string Reason;
	CHECK_FALSE(FPhysicsBackendRegistry::IsChaosAvailable(Reason));
	CHECK_FALSE(Reason.empty());
}

TEST_CASE("PhysicsTest.Abi.CallOverheadNull")
{
	// 03 M2: the ABI cost of Null with 0 bodies, 4 calls per frame (SetUpForFrame, StartFrame, WaitPhysScenes, EndFrame),
	// as a QPC batch average. Recorded only (the TSC-based 100 ns gate is M10).
	for (const char* Name : GAbiTargets)
	{
		AbiGates::FAbiGateTarget Target;
		std::string Error;
		REQUIRE_MESSAGE(AbiGates::PrepareAbiGateTarget(Name, Target, Error), Error);
		const PtBackendApi& Api = *Target.Api;
		const PtInitParams Init = MakeInit();
		REQUIRE(Api.InitGamePhys(&Init) == PT_OK);
		const PtSceneDesc Desc = MakeDesc();
		PtSceneId Scene = 0;
		REQUIRE(Api.CreateScene(&Desc, &Scene) == PT_OK);
		const PtVec3 Gravity = { 0.0, 0.0, -980.0 };
		const auto RunFrames = [&](const int32 Frames)
		{
			bool bOk = true;
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				PtFrameResult Result;
				bOk = Api.SetUpForFrame(Scene, &Gravity, 1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 1, 0u) == PT_OK && Api.StartFrame(Scene) == PT_OK
					&& Api.WaitPhysScenes(Scene) == PT_OK && Api.EndFrame(Scene, &Result) == PT_OK && bOk;
			}
			return bOk;
		};
		REQUIRE(RunFrames(10000));   // warm-up
		double Best = 1e30;
		const int32 Frames = 200000;
		for (int32 Batch = 0; Batch < 5; ++Batch)
		{
			const uint64 Begin = FPlatformTime::Cycles64();
			REQUIRE(RunFrames(Frames));
			const uint64 End = FPlatformTime::Cycles64();
			const double NsPerCall = FPlatformTime::ToSeconds64(End - Begin) * 1.0e9 / (4.0 * Frames);
			Best = NsPerCall < Best ? NsPerCall : Best;
		}
		char Line[200];
		std::snprintf(Line, sizeof(Line), "ABI call cost, %s, 0 bodies: %.1f ns per call (best of 5 batches of %d frames x 4 calls, QPC %llu Hz)", Name, Best,
			Frames, static_cast<unsigned long long>(FPlatformTime::GetCyclesPerSecond64()));
		MESSAGE(Line);
		std::printf("PT_ABI_COST %s\n", Line);
		CHECK(Best > 0.0);
		CHECK(Api.DestroyScene(Scene) == PT_OK);
		CHECK(Api.TermGamePhys() == PT_OK);
	}
}

namespace
{
	// Three bodies without gravity for the frame-rule checks: 0 dynamic sphere (2 kg, I 1000), 1 kinematic sphere at the
	// origin, 2 dynamic sphere that starts asleep.
	PtSceneId MakeRuleScene(const PtBackendApi& Api, const double AsyncDt)
	{
		PtSceneDesc Desc = MakeDesc();
		Desc.SimFlags = PT_SIM_ALLOW_SLEEPING;
		Desc.AsyncFixedTimeStepSize = AsyncDt;
		PtSceneId Scene = 0;
		REQUIRE(Api.CreateScene(&Desc, &Scene) == PT_OK);
		PtPhysicalMaterial Material = {};
		Material.Friction = 0.5;
		REQUIRE(Api.CreateMaterials(Scene, &Material, 1, sizeof(PtPhysicalMaterial)) == PT_OK);
		PtShapeElem Shape = {};
		Shape.LocalTransform.Rotation.W = 1.0;
		Shape.Type = PT_SHAPE_SPHERE;
		Shape.Radius = 10.0;
		Shape.ContactOffset = -1.0;
		Shape.Flags = PT_SHAPEF_SIMULATION;
		PtActorCreateInfo Actors[3] = {};
		for (uint32 Index = 0; Index < 3; ++Index)
		{
			PtActorCreateInfo& Actor = Actors[Index];
			Actor.BodyIndex = Index;
			Actor.Motion = Index == 1 ? PT_MOTION_KINEMATIC : PT_MOTION_DYNAMIC;
			Actor.Flags = PT_BODY_ALLOW_SLEEP | (Index == 2 ? 0u : PT_BODY_START_AWAKE);
			Actor.NumShapes = 1;
			Actor.ActorTransform.Rotation.W = 1.0;
			Actor.ActorTransform.Translation = PtVec3{ Index == 1 ? 0.0 : 100.0 * Index + 100.0, 0.0, 0.0 };
			Actor.CenterOfMassLocal.Rotation.W = 1.0;
			Actor.Mass = 2.0;
			Actor.InertiaDiagonal = PtVec3{ 1000.0, 1000.0, 1000.0 };
			Actor.SleepThresholdMultiplier = 1.0;
			Actor.StabilizationThresholdMultiplier = 1.0;
		}
		PtActorBatch Batch = {};
		Batch.StructSize = sizeof(PtActorBatch);
		Batch.NumActors = 3;
		Batch.Actors = Actors;
		Batch.ActorStride = sizeof(PtActorCreateInfo);
		Batch.NumShapes = 1;
		Batch.Shapes = &Shape;
		Batch.ShapeStride = sizeof(PtShapeElem);
		REQUIRE(Api.CreateActors(Scene, &Batch) == PT_OK);
		return Scene;
	}

	PtBodyCommand MakeRuleCommand(const uint32 Type, const uint32 Body, const PtVec3& Vector, const uint32 Flags = 0u)
	{
		PtBodyCommand Command = {};
		Command.Type = Type;
		Command.Body = Body;
		Command.Flags = Flags;
		Command.Transform.Rotation.W = 1.0;
		Command.Vector = Vector;
		return Command;
	}

	void ApplyRuleCommand(const PtBackendApi& Api, const PtSceneId Scene, const PtBodyCommand& Command)
	{
		REQUIRE(Api.ApplyBodyCommands(Scene, &Command, 1, sizeof(PtBodyCommand)) == PT_OK);
	}

	// One native frame with UE's SetUpForFrame arguments (no clamp, MinDeltaTime 0); returns NumStepsTaken.
	uint32 RunRuleFrame(const PtBackendApi& Api, const PtSceneId Scene, const float Dt, const float MaxSubDt, const int32 MaxSubsteps, const bool bSubstepping)
	{
		const PtVec3 Gravity = { 0.0, 0.0, -980.0 };
		PtFrameResult Result = {};
		REQUIRE(Api.SetUpForFrame(Scene, &Gravity, Dt, 0.0f, 0.0f, MaxSubDt, MaxSubsteps, bSubstepping ? 1u : 0u) == PT_OK);
		REQUIRE(Api.StartFrame(Scene) == PT_OK);
		REQUIRE(Api.WaitPhysScenes(Scene) == PT_OK);
		REQUIRE(Api.EndFrame(Scene, &Result) == PT_OK);
		return Result.NumStepsTaken;
	}

	PtBodyState ReadRuleState(const PtBackendApi& Api, const PtSceneId Scene, const uint32 Body)
	{
		PtBodyState States[3] = {};
		uint32 Count = 0;
		REQUIRE(Api.ReadBodyStates(Scene, PT_READ_ALL, nullptr, States, 3, &Count) == PT_OK);
		REQUIRE(Count == 3u);
		return States[Body];
	}
}

TEST_CASE("PhysicsTest.Abi.NullNativeFrameRules")
{
	// UE5's per-frame command rules in a NATIVE_FRAME_MODEL backend (PtBackendAbi.h body commands, docs/Abi.md 5.10),
	// through the raw C table of the static and the DLL Null backend:
	//   forces act on every step of a frame that pushes steps, PT_CMDF_ALLOW_SUBSTEPPING or not (CopySubstepData);
	//   impulses on the first step; a pushing frame without a step consumes the forces; AsyncFixed frames that push
	//   nothing carry them and the push averages them (1 / ExternalSteps); AddForce / AddTorque wake a sleeping body even
	//   with a zero vector, unless flagged PT_CMDF_NO_WAKE (then a sleeping body ignores them); a teleport always wakes
	//   and zeroes a kinematic body's velocity; kinematic targets are interpolated by 1 / (N - k) when the solver
	//   substeps, else reached on the first step (the next step zeroes the kinematic velocity).
	const float Sixtieth = 1.0f / 60.0f;
	const double Pi = 3.14159265358979323846;
	for (const char* Name : GAbiTargets)
	{
		CAPTURE(Name);
		AbiGates::FAbiGateTarget Target;
		std::string Error;
		REQUIRE_MESSAGE(AbiGates::PrepareAbiGateTarget(Name, Target, Error), Error);
		const PtBackendApi& Api = *Target.Api;
		const PtInitParams Init = MakeInit();
		REQUIRE(Api.InitGamePhys(&Init) == PT_OK);

		// UE5 substepping, dt 0.05 -> 3 steps of D / 3 (FReal).
		{
			const PtSceneId Scene = MakeRuleScene(Api, PT_ASYNC_DT_SYNC);
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_ADD_FORCE, 0, PtVec3{ 60.0, 0.0, 0.0 }));     // 30 cm/s^2, no ALLOW flag
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_ADD_IMPULSE, 0, PtVec3{ 0.0, 20.0, 0.0 }));   // dv 10 cm/s, once
			REQUIRE(RunRuleFrame(Api, Scene, 0.05f, Sixtieth, 6, true) == 3u);
			const PtBodyState First = ReadRuleState(Api, Scene, 0);
			CHECK(First.LinearVelocity.X == doctest::Approx(30.0 * static_cast<double>(0.05f)).epsilon(1e-12));   // all 3 steps, not 1
			CHECK(First.LinearVelocity.Y == 10.0);
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_ADD_FORCE, 0, PtVec3{ 60.0, 0.0, 0.0 }, PT_CMDF_ALLOW_SUBSTEPPING));
			REQUIRE(RunRuleFrame(Api, Scene, 0.05f, Sixtieth, 6, true) == 3u);
			const PtBodyState Second = ReadRuleState(Api, Scene, 0);
			CHECK(Second.LinearVelocity.X - First.LinearVelocity.X == doctest::Approx(First.LinearVelocity.X).epsilon(1e-12));   // the flag changes nothing
			CHECK(Second.LinearVelocity.Y == 10.0);

			// Kinematic: target 90 cm and 90 degrees about Z in 3 substeps -> 30 cm and 30 degrees per step (1 / (N - k)).
			PtBodyCommand Move = MakeRuleCommand(PT_CMD_SET_KINEMATIC_TARGET, 1, PtVec3{});
			Move.Transform.Rotation = PtQuat{ 0.0, 0.0, std::sin(0.25 * Pi), std::cos(0.25 * Pi) };
			Move.Transform.Translation = PtVec3{ 90.0, 0.0, 0.0 };
			ApplyRuleCommand(Api, Scene, Move);
			REQUIRE(RunRuleFrame(Api, Scene, 0.05f, Sixtieth, 6, true) == 3u);
			const PtBodyState Kinematic = ReadRuleState(Api, Scene, 1);
			const double StepDt = static_cast<double>(0.05f) / 3.0;
			CHECK(Kinematic.ActorTransform.Translation.X == doctest::Approx(90.0).epsilon(1e-12));
			CHECK(Kinematic.ActorTransform.Rotation.Z == doctest::Approx(Move.Transform.Rotation.Z).epsilon(1e-12));
			CHECK(Kinematic.LinearVelocity.X == doctest::Approx(30.0 / StepDt).epsilon(1e-9));
			CHECK(Kinematic.AngularVelocity.Z == doctest::Approx((Pi / 6.0) / StepDt).epsilon(1e-9));
			REQUIRE(RunRuleFrame(Api, Scene, 0.05f, Sixtieth, 6, true) == 3u);
			CHECK(ReadRuleState(Api, Scene, 1).LinearVelocity.X == 0.0);   // no new target: velocity reset

			// Wake: AddForce / AddTorque with a zero vector wake a sleeping body (the game-thread proxy wakes on every call).
			CHECK(ReadRuleState(Api, Scene, 2).ObjectState == PT_OBJECT_STATE_SLEEPING);
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_ADD_FORCE, 2, PtVec3{}));
			CHECK(ReadRuleState(Api, Scene, 2).ObjectState == PT_OBJECT_STATE_DYNAMIC);
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_PUT_TO_SLEEP, 2, PtVec3{}));
			CHECK(ReadRuleState(Api, Scene, 2).ObjectState == PT_OBJECT_STATE_SLEEPING);
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_ADD_TORQUE, 2, PtVec3{}));
			CHECK(ReadRuleState(Api, Scene, 2).ObjectState == PT_OBJECT_STATE_DYNAMIC);

			// PT_CMDF_NO_WAKE (the host sends UE's game-thread wake separately, in issue order): a sleeping body neither
			// wakes nor moves; an awake body takes the force as usual.
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_PUT_TO_SLEEP, 2, PtVec3{}));
			const PtBodyState Dozing = ReadRuleState(Api, Scene, 2);
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_ADD_FORCE, 2, PtVec3{ 600.0, 0.0, 0.0 }, PT_CMDF_NO_WAKE));
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_ADD_TORQUE, 2, PtVec3{ 0.0, 0.0, 600.0 }, PT_CMDF_NO_WAKE));
			CHECK(ReadRuleState(Api, Scene, 2).ObjectState == PT_OBJECT_STATE_SLEEPING);
			const double AwakeBefore = ReadRuleState(Api, Scene, 0).LinearVelocity.X;
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_ADD_FORCE, 0, PtVec3{ 60.0, 0.0, 0.0 }, PT_CMDF_NO_WAKE));
			REQUIRE(RunRuleFrame(Api, Scene, 0.05f, Sixtieth, 6, true) == 3u);
			const PtBodyState StillDozing = ReadRuleState(Api, Scene, 2);
			CHECK(StillDozing.ObjectState == PT_OBJECT_STATE_SLEEPING);
			CHECK(std::memcmp(&StillDozing.ActorTransform, &Dozing.ActorTransform, sizeof(PtTransform)) == 0);
			CHECK(StillDozing.LinearVelocity.X == 0.0);
			CHECK(ReadRuleState(Api, Scene, 0).LinearVelocity.X - AwakeBefore == doctest::Approx(30.0 * static_cast<double>(0.05f)).epsilon(1e-12));

			// A teleport wakes a sleeping body whatever PT_CMDF_AUTO_WAKE says (UE5's SetX), and on a kinematic body it also
			// becomes the target with zero velocity at once (ChaosEngineInterface.cpp:2550-2557).
			PtBodyCommand Teleport = MakeRuleCommand(PT_CMD_SET_GLOBAL_POSE, 2, PtVec3{});
			Teleport.Transform.Translation = PtVec3{ 300.0, 50.0, 0.0 };
			ApplyRuleCommand(Api, Scene, Teleport);
			CHECK(ReadRuleState(Api, Scene, 2).ObjectState == PT_OBJECT_STATE_DYNAMIC);
			PtBodyCommand Ahead = MakeRuleCommand(PT_CMD_SET_KINEMATIC_TARGET, 1, PtVec3{});
			Ahead.Transform.Translation = PtVec3{ 150.0, 0.0, 0.0 };
			ApplyRuleCommand(Api, Scene, Ahead);
			REQUIRE(RunRuleFrame(Api, Scene, 0.05f, Sixtieth, 6, true) == 3u);
			CHECK(ReadRuleState(Api, Scene, 1).LinearVelocity.X > 0.0);
			PtBodyCommand Jump = MakeRuleCommand(PT_CMD_SET_GLOBAL_POSE, 1, PtVec3{}, PT_CMDF_AUTO_WAKE);
			Jump.Transform.Translation = PtVec3{ -50.0, 0.0, 0.0 };
			ApplyRuleCommand(Api, Scene, Jump);
			const PtBodyState Jumped = ReadRuleState(Api, Scene, 1);
			CHECK(Jumped.ActorTransform.Translation.X == -50.0);
			CHECK(Jumped.LinearVelocity.X == 0.0);
			CHECK(Jumped.AngularVelocity.Z == 0.0);
			REQUIRE(RunRuleFrame(Api, Scene, 0.05f, Sixtieth, 6, true) == 3u);
			CHECK(ReadRuleState(Api, Scene, 1).ActorTransform.Translation.X == -50.0);   // the teleport is its target: it stays
			CHECK(ReadRuleState(Api, Scene, 1).LinearVelocity.X == 0.0);

			// Sync dt 0: one pushed step that does not run consumes the force; the next frame does not see it.
			const double Before = ReadRuleState(Api, Scene, 0).LinearVelocity.X;
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_ADD_FORCE, 0, PtVec3{ 600.0, 0.0, 0.0 }));
			CHECK(RunRuleFrame(Api, Scene, 0.0f, 0.0f, 1, false) == 0u);
			CHECK(RunRuleFrame(Api, Scene, Sixtieth, 0.0f, 1, false) == 1u);
			CHECK(ReadRuleState(Api, Scene, 0).LinearVelocity.X == Before);
			CHECK(Api.DestroyScene(Scene) == PT_OK);
		}

		// AsyncFixed 1/30 at 1/60 frames: frame A pushes nothing (the force waits), frame B pushes one step with the
		// average of both frames' forces (ExternalSteps 2).
		{
			volatile float Thirtieth = 1.0f / 30.0f;   // a runtime float: /fp:fast folds static_cast<double>(1.0f / 30.0f) to 1/30 in double
			const double AsyncDt = Thirtieth;
			const PtSceneId Scene = MakeRuleScene(Api, AsyncDt);
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_ADD_FORCE, 0, PtVec3{ 60.0, 0.0, 0.0 }));    // 30 cm/s^2
			REQUIRE(RunRuleFrame(Api, Scene, Sixtieth, 0.0f, 1, false) == 0u);
			CHECK(ReadRuleState(Api, Scene, 0).LinearVelocity.X == 0.0);
			ApplyRuleCommand(Api, Scene, MakeRuleCommand(PT_CMD_ADD_FORCE, 0, PtVec3{ 120.0, 0.0, 0.0 }));   // 60 cm/s^2
			REQUIRE(RunRuleFrame(Api, Scene, Sixtieth, 0.0f, 1, false) == 1u);
			CHECK(ReadRuleState(Api, Scene, 0).LinearVelocity.X == doctest::Approx(45.0 * AsyncDt).epsilon(1e-12));
			CHECK(Api.DestroyScene(Scene) == PT_OK);
		}

		// AsyncFixed 1/60 with a 1/30 frame: two pushed steps, not solver-substepped -> the kinematic target is reached on
		// the first step, the second step zeroes its velocity (Chaos mode Reset).
		{
			const PtSceneId Scene = MakeRuleScene(Api, static_cast<double>(Sixtieth));
			PtBodyCommand Move = MakeRuleCommand(PT_CMD_SET_KINEMATIC_TARGET, 1, PtVec3{});
			Move.Transform.Translation = PtVec3{ 60.0, 0.0, 0.0 };
			ApplyRuleCommand(Api, Scene, Move);
			REQUIRE(RunRuleFrame(Api, Scene, 1.0f / 30.0f, 0.0f, 1, false) == 2u);
			const PtBodyState Kinematic = ReadRuleState(Api, Scene, 1);
			CHECK(Kinematic.ActorTransform.Translation.X == 60.0);
			CHECK(Kinematic.LinearVelocity.X == 0.0);
			CHECK(Api.DestroyScene(Scene) == PT_OK);
		}
		CHECK(Api.TermGamePhys() == PT_OK);
	}
}

TEST_CASE("PhysicsTest.Abi.TextWriterTerminatesOnOverflow")
{
	// A text that overflows on its first piece still leaves a valid (empty) C string, and PrepareForSimulation truncates
	// into a tiny buffer the same way (PT_W_TRUNCATED, Length = the bytes needed).
	char Data[8];
	std::memset(Data, 'X', sizeof(Data));
	PtTextBuffer Buffer = { Data, 4, 0 };
	Abi::FTextWriter Writer(&Buffer);
	Writer.Append("longer than four");
	CHECK(Writer.Finish() == PT_E_CAPACITY);
	CHECK(Data[0] == '\0');
	CHECK(Buffer.Length == 17u);

	AbiGates::FAbiGateTarget Target;
	std::string Error;
	REQUIRE_MESSAGE(AbiGates::PrepareAbiGateTarget("Null", Target, Error), Error);
	const PtBackendApi& Api = *Target.Api;
	const PtInitParams Init = MakeInit();
	REQUIRE(Api.InitGamePhys(&Init) == PT_OK);
	const PtSceneDesc Desc = MakeDesc();
	PtSceneId Scene = 0;
	REQUIRE(Api.CreateScene(&Desc, &Scene) == PT_OK);
	std::memset(Data, 'X', sizeof(Data));
	PtTextBuffer Tiny = { Data, 4, 0 };
	CHECK(Api.PrepareForSimulation(Scene, &Tiny) == PT_W_TRUNCATED);
	CHECK(Data[0] == '\0');
	CHECK(Tiny.Length > 4u);
	CHECK(Api.TermGamePhys() == PT_OK);
}
