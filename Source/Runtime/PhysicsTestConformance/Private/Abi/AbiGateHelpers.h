// AbiGateHelpers.h - small builders shared by the ABI gates (raw C-table calls with valid default arguments).
#pragma once

#include "Abi/AbiGates.h"
#include "Physics/PhysicsBackend.h"

#include <immintrin.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace PhysicsTest::AbiGates::AbiGateHelpers
{
	inline FGateReport MakeReport(const char* Gate, const FAbiGateTarget& Target)
	{
		FGateReport Report;
		Report.Gate = Gate;
		Report.Target = Target.Label;
		return Report;
	}

	// Gates end with the module terminated whatever happened (a failed check must not leave it Ready).
	struct FTerminateOnExit
	{
		const PtBackendApi& Api;
		explicit FTerminateOnExit(const PtBackendApi& InApi) : Api(InApi) {}
		~FTerminateOnExit() { Api.TermGamePhys(); }
		FTerminateOnExit(const FTerminateOnExit&) = delete;
		FTerminateOnExit& operator=(const FTerminateOnExit&) = delete;
	};

	inline PtInitParams MakeInitParams()
	{
		PtInitParams Params = {};
		Params.StructSize = sizeof(PtInitParams);
		Params.MaxThreads = 1;
		Params.EngineCommandLine = "";
		Params.SavedDir = "";
		return Params;
	}

	inline PtSceneDesc MakeSceneDesc(const uint32 SimFlags = PT_SIM_ALLOW_SLEEPING, const double AsyncDt = PT_ASYNC_DT_SYNC)
	{
		PtSceneDesc Desc = {};
		Desc.StructSize = sizeof(PtSceneDesc);
		Desc.RunMode = PT_RUN_BEHAVIOR;
		Desc.SimFlags = SimFlags;
		Desc.DebugName = "AbiGate";
		Desc.Gravity = PtVec3{ 0.0, 0.0, -980.0 };
		Desc.NominalDeltaTime = 1.0 / 60.0;
		Desc.AsyncFixedTimeStepSize = AsyncDt;
		Desc.RestitutionThreshold = -1.0;
		Desc.NumThreads = 1;
		return Desc;
	}

	inline PtPhysicalMaterial MakeMaterial()
	{
		PtPhysicalMaterial Material = {};
		Material.Friction = 0.7;
		Material.StaticFriction = 0.0;
		Material.Restitution = 0.3;
		Material.SleepLinearVelocityThreshold = 1.0;
		Material.SleepAngularVelocityThreshold = 0.05;
		Material.SleepCounterThreshold = 4;
		return Material;
	}

	inline PtQuat Normalized(const double X, const double Y, const double Z, const double W)
	{
		const double Size = std::sqrt(X * X + Y * Y + Z * Z + W * W);
		return PtQuat{ X / Size, Y / Size, Z / Size, W / Size };
	}

	inline PtTransform MakeTransform(const PtVec3& Translation, const PtQuat& Rotation = PtQuat{ 0.0, 0.0, 0.0, 1.0 })
	{
		return PtTransform{ Rotation, Translation };
	}

	// Five bodies: 0 static ground box, 1 falling sphere (1 kg), 2 spinning damped box with an offset COM and initial
	// velocities, 3 kinematic box, 4 sphere with !START_AWAKE (asleep when the scene allows sleeping).
	struct FTestScene
	{
		std::vector<PtActorCreateInfo> Actors;
		std::vector<PtShapeElem> Shapes;
		uint32 NumBodies = 0;
		uint32 FirstDynamic = 0;   // body index of the falling sphere
		uint32 Kinematic = 0;      // body index of the kinematic box
		PtResult Result = PT_OK;

		PtActorBatch MakeBatch() const
		{
			PtActorBatch Batch = {};
			Batch.StructSize = sizeof(PtActorBatch);
			Batch.NumActors = static_cast<uint32>(Actors.size());
			Batch.Actors = Actors.data();
			Batch.ActorStride = sizeof(PtActorCreateInfo);
			Batch.NumShapes = static_cast<uint32>(Shapes.size());
			Batch.Shapes = Shapes.data();
			Batch.ShapeStride = sizeof(PtShapeElem);
			return Batch;
		}
	};

	inline FTestScene MakeTestBodies(const uint32 FirstIndex, const uint32 MaterialIndex)
	{
		FTestScene Scene;
		const auto AddShape = [&Scene, MaterialIndex](const uint32 Type, const double Radius, const PtVec3& HalfExtent)
		{
			PtShapeElem Shape = {};
			Shape.LocalTransform = MakeTransform(PtVec3{ 0.0, 0.0, 0.0 });
			Shape.Type = Type;
			Shape.MaterialIndex = MaterialIndex;
			Shape.Radius = Radius;
			Shape.BoxHalfExtent = HalfExtent;
			Shape.ContactOffset = -1.0;
			Shape.Flags = PT_SHAPEF_SIMULATION | PT_SHAPEF_QUERY;
			Scene.Shapes.push_back(Shape);
			return static_cast<uint32>(Scene.Shapes.size() - 1);
		};
		const auto AddActor = [&Scene, FirstIndex](const uint32 Motion, const uint32 Shape, const PtTransform& Pose, const double Mass, const PtVec3& Inertia)
		{
			PtActorCreateInfo Actor = {};
			Actor.BodyIndex = FirstIndex + static_cast<uint32>(Scene.Actors.size());
			Actor.Motion = Motion;
			Actor.Flags = PT_BODY_ENABLE_GRAVITY | PT_BODY_START_AWAKE | PT_BODY_ALLOW_SLEEP;
			Actor.FirstShape = Shape;
			Actor.NumShapes = 1;
			Actor.BlockMask = 0xFFFFFFFFu;
			Actor.PositionIterations = 8;
			Actor.VelocityIterations = 2;
			Actor.ProjectionIterations = 1;
			Actor.ActorTransform = Pose;
			Actor.Mass = Mass;
			Actor.InertiaDiagonal = Inertia;
			Actor.CenterOfMassLocal = MakeTransform(PtVec3{ 0.0, 0.0, 0.0 });
			Actor.SleepThresholdMultiplier = 1.0;
			Actor.StabilizationThresholdMultiplier = 1.0;
			Scene.Actors.push_back(Actor);
			return Scene.Actors.size() - 1;
		};
		AddActor(PT_MOTION_STATIC, AddShape(PT_SHAPE_BOX, 0.0, PtVec3{ 1000.0, 1000.0, 10.0 }), MakeTransform(PtVec3{ 0.0, 0.0, -10.0 }), 0.0, PtVec3{ 0.0, 0.0, 0.0 });
		const size_t Sphere = AddActor(PT_MOTION_DYNAMIC, AddShape(PT_SHAPE_SPHERE, 50.0, PtVec3{}), MakeTransform(PtVec3{ 0.0, 0.0, 1000.0 }), 1.0,
			PtVec3{ 1000.0, 1000.0, 1000.0 });
		// 100 x 50 x 20 cm, 10 kg: I = m / 12 (b^2 + c^2) per axis.
		const size_t Box = AddActor(PT_MOTION_DYNAMIC, AddShape(PT_SHAPE_BOX, 0.0, PtVec3{ 50.0, 25.0, 10.0 }),
			MakeTransform(PtVec3{ 300.0, -200.0, 600.0 }, Normalized(0.1, 0.2, 0.3, 0.9)), 10.0,
			PtVec3{ 10.0 / 12.0 * (50.0 * 50.0 + 20.0 * 20.0), 10.0 / 12.0 * (100.0 * 100.0 + 20.0 * 20.0), 10.0 / 12.0 * (100.0 * 100.0 + 50.0 * 50.0) });
		Scene.Actors[Box].InitialLinearVelocity = PtVec3{ 100.0, 0.0, 50.0 };
		Scene.Actors[Box].InitialAngularVelocity = PtVec3{ 0.5, 1.0, 2.0 };
		Scene.Actors[Box].LinearDamping = 0.1;
		Scene.Actors[Box].AngularDamping = 0.05;
		Scene.Actors[Box].MaxAngularVelocity = 50.0;
		Scene.Actors[Box].CenterOfMassLocal = MakeTransform(PtVec3{ 0.0, 0.0, 5.0 }, Normalized(0.0, 0.0, 0.3826834, 0.9238795));
		const size_t Kinematic = AddActor(PT_MOTION_KINEMATIC, AddShape(PT_SHAPE_BOX, 0.0, PtVec3{ 20.0, 20.0, 20.0 }), MakeTransform(PtVec3{ -300.0, 0.0, 100.0 }), 5.0,
			PtVec3{ 100.0, 100.0, 100.0 });
		const size_t Asleep = AddActor(PT_MOTION_DYNAMIC, AddShape(PT_SHAPE_SPHERE, 25.0, PtVec3{}), MakeTransform(PtVec3{ 0.0, 300.0, 200.0 }), 2.0,
			PtVec3{ 500.0, 500.0, 500.0 });
		Scene.Actors[Asleep].Flags &= ~PT_BODY_START_AWAKE;
		Scene.NumBodies = static_cast<uint32>(Scene.Actors.size());
		Scene.FirstDynamic = Scene.Actors[Sphere].BodyIndex;
		Scene.Kinematic = Scene.Actors[Kinematic].BodyIndex;
		return Scene;
	}

	inline std::string LastError(const PtBackendApi& Api)
	{
		char Text[1100] = {};
		PtTextBuffer Buffer = { Text, sizeof(Text), 0 };
		Api.GetLastErrorMessage(&Buffer);
		return std::string(Text);
	}

	// One material + the five test bodies (indices 0..4 in a fresh scene).
	inline FTestScene PopulateScene(const PtBackendApi& Api, const PtSceneId Scene)
	{
		const PtPhysicalMaterial Material = MakeMaterial();
		FTestScene Bodies = MakeTestBodies(0, 0);
		Bodies.Result = Api.CreateMaterials(Scene, &Material, 1, sizeof(PtPhysicalMaterial));
		if (Bodies.Result == PT_OK)
		{
			const PtActorBatch Batch = Bodies.MakeBatch();
			Bodies.Result = Api.CreateActors(Scene, &Batch);
		}
		return Bodies;
	}

	// The same through arrays whose element structs carry extra trailing fields (a newer host): strides > sizeof.
	inline FTestScene PopulateScenePadded(const PtBackendApi& Api, const PtSceneId Scene)
	{
		struct FPaddedMaterial
		{
			PtPhysicalMaterial Material;
			uint64 Tail[2];
		};
		struct FPaddedActor
		{
			PtActorCreateInfo Actor;
			uint64 Tail[5];
		};
		struct FPaddedShape
		{
			PtShapeElem Shape;
			uint64 Tail[3];
		};
		FPaddedMaterial Material = {};
		Material.Material = MakeMaterial();
		Material.Tail[0] = Material.Tail[1] = 0xFFFFFFFFFFFFFFFFull;
		FTestScene Bodies = MakeTestBodies(0, 0);
		std::vector<FPaddedActor> Actors(Bodies.Actors.size());
		std::vector<FPaddedShape> Shapes(Bodies.Shapes.size());
		for (size_t Index = 0; Index < Actors.size(); ++Index)
		{
			Actors[Index].Actor = Bodies.Actors[Index];
			std::memset(Actors[Index].Tail, 0xCD, sizeof(Actors[Index].Tail));
		}
		for (size_t Index = 0; Index < Shapes.size(); ++Index)
		{
			Shapes[Index].Shape = Bodies.Shapes[Index];
			std::memset(Shapes[Index].Tail, 0xCD, sizeof(Shapes[Index].Tail));
		}
		Bodies.Result = Api.CreateMaterials(Scene, &Material.Material, 1, sizeof(FPaddedMaterial));
		if (Bodies.Result == PT_OK)
		{
			PtActorBatch Batch = Bodies.MakeBatch();
			Batch.Actors = &Actors[0].Actor;
			Batch.ActorStride = sizeof(FPaddedActor);
			Batch.Shapes = &Shapes[0].Shape;
			Batch.ShapeStride = sizeof(FPaddedShape);
			Bodies.Result = Api.CreateActors(Scene, &Batch);
		}
		return Bodies;
	}

	inline PtBodyCommand MakeCommand(const uint32 Type, const uint32 Body)
	{
		PtBodyCommand Command = {};
		Command.Type = Type;
		Command.Body = Body;
		Command.Transform = MakeTransform(PtVec3{ 0.0, 0.0, 0.0 });
		return Command;
	}

	inline PtConstraintCreateInfo MakeJoint(const uint32 Body1, const uint32 Body2)
	{
		PtConstraintCreateInfo Joint = {};
		Joint.Type = PT_JOINT_BALL;
		Joint.Body1 = Body1;
		Joint.Body2 = Body2;
		Joint.Frame1 = MakeTransform(PtVec3{ 0.0, 0.0, 0.0 });
		Joint.Frame2 = MakeTransform(PtVec3{ 0.0, 0.0, 0.0 });
		return Joint;
	}

	// One Harness-style frame: SetUpForFrame(dt, 0, 0, 0, 1, false) -> StartFrame -> WaitPhysScenes -> EndFrame.
	inline PtResult StepFrame(const PtBackendApi& Api, const PtSceneId Scene, const float Dt = 1.0f / 60.0f)
	{
		const PtVec3 Gravity = { 0.0, 0.0, -980.0 };
		PtResult Result = Api.SetUpForFrame(Scene, &Gravity, Dt, 0.0f, 0.0f, 0.0f, 1, 0u);
		if (Result != PT_OK)
		{
			return Result;
		}
		Result = Api.StartFrame(Scene);
		if (Result != PT_OK)
		{
			return Result;
		}
		Result = Api.WaitPhysScenes(Scene);
		if (Result != PT_OK)
		{
			return Result;
		}
		PtFrameResult Frame = {};
		return Api.EndFrame(Scene, &Frame);
	}

	inline std::vector<PtBodyState> ReadAllStates(const PtBackendApi& Api, const PtSceneId Scene)
	{
		uint32 Count = 0;
		Api.ReadBodyStates(Scene, PT_READ_ALL, nullptr, nullptr, 0, &Count);
		std::vector<PtBodyState> States(Count);
		if (Count == 0 || Api.ReadBodyStates(Scene, PT_READ_ALL, nullptr, States.data(), Count, &Count) != PT_OK)
		{
			States.clear();
		}
		return States;
	}

	inline uint64 ReadHash(const PtBackendApi& Api, const PtSceneId Scene)
	{
		uint64 Hash = 0;
		return Api.ComputeNativeStateHash(Scene, &Hash) == PT_OK ? Hash : 0;
	}

	inline bool SameStates(const std::vector<PtBodyState>& A, const std::vector<PtBodyState>& B)
	{
		return A.size() == B.size() && (A.empty() || std::memcmp(A.data(), B.data(), A.size() * sizeof(PtBodyState)) == 0);
	}

	inline std::string DumpText(const PtBackendApi& Api, const PtSceneId SceneOrZero)
	{
		PtTextBuffer Query = {};
		Api.DumpEffectiveSettings(SceneOrZero, &Query);
		std::string Text(Query.Length > 0 ? Query.Length : 1u, '\0');
		PtTextBuffer Buffer = { Text.data(), static_cast<uint32_t>(Text.size()), 0 };
		if (Api.DumpEffectiveSettings(SceneOrZero, &Buffer) != PT_OK)
		{
			return std::string();
		}
		Text.resize(Buffer.Length > 0 ? Buffer.Length - 1 : 0);
		return Text;
	}

	// MXCSR control bits (6-15) and the x87 control word. The x87 word is read and written through FXSAVE / FXRSTOR in
	// one non-inlined function (x64 MSVC has no other access): the XMM registers restored by FXRSTOR are the ones saved
	// by FXSAVE a few instructions earlier in the same frame, so nothing else changes.
	struct FFpState
	{
		uint32 MxcsrControl = 0;
		uint16 X87Control = 0;
	};

	__declspec(noinline) inline FFpState ReadFpState()
	{
		alignas(16) unsigned char Area[512];
		_fxsave64(Area);
		FFpState State;
		std::memcpy(&State.X87Control, Area, sizeof(State.X87Control));
		State.MxcsrControl = _mm_getcsr() & 0xFFC0u;
		return State;
	}

	__declspec(noinline) inline void WriteFpState(const FFpState& State)
	{
		_mm_setcsr((_mm_getcsr() & 0x3Fu) | (State.MxcsrControl & 0xFFC0u));
		alignas(16) unsigned char Area[512];
		_fxsave64(Area);
		std::memcpy(Area, &State.X87Control, sizeof(State.X87Control));
		_fxrstor64(Area);
	}

	inline std::string ToHex(const uint32 Value)
	{
		char Text[16];
		snprintf(Text, sizeof(Text), "%04X", Value);
		return Text;
	}
}
