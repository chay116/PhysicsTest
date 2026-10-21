// DebugDrawTests.cpp - PT::PhysicsDebugDraw (03_WorkPlan M4): the unit wire meshes (12 / 128 / 68 / 8), the Chaos colour
// tables, the command stream order of FPBDRigidsSolver::PostTickDebugDraw (9 stages), the ABI -> DrawDebug conversions
// (capsule HalfHeight = HalfLength + R, box half extent), contact discs and normals, COM axes, FIslandBuilder, the capture
// stage (FChaosDDPrimitives costs, command bounds, particle shapes) and the render stage (FChaosDDRenderer budget and region)
// of UE 5.8.3, the IsDynamic colours of sleeping and kinematic bodies, the host snapshot for Null, the joint drawer on a
// synthetic snapshot, Jolt's per-point Phi, sleeping-island contacts and the inertia box frame. Renderer-free: runs in every
// preset.
#include "CoreMinimal.h"

#include "BenchmarkRunner.h"
#include "Containers/StringConv.h"
#include "DebugDrawQueue.h"
#include "DebugDrawRenderer.h"
#include "DebugDrawSettings.h"
#include "IslandBuilder.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsDebugSnapshot.h"
#include "Physics/PhysicsInterface.h"
#include "Physics/PhysicsProfile.h"
#include "PhysicsDebugDraw.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "PTBuildInfo.h"
#include "WireMeshes.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace PhysicsTest;

namespace
{
	using EDrawType = FLatentDrawCommand::EDrawType;

	std::filesystem::path DebugDrawConfigDir()
	{
		std::string Override;
		if (FParse::Value(FCommandLine::Get(), "ConfigDir=", Override))
		{
			return StringConv::Utf8ToPath(Override);
		}
		return StringConv::Utf8ToPath(PT_CONFIG_DIR);
	}

	// A Null scene with a static ground box (index 0), a dynamic 100 cm box (1), a dynamic capsule (2), a dynamic sphere (3)
	// and a kinematic box (4), plus one joint between bodies 1 and 2.
	struct FTestScene
	{
		std::unique_ptr<FPhysScene> Scene;
		FBodyInstance* Ground = nullptr;
		FBodyInstance* Box = nullptr;
		FBodyInstance* Capsule = nullptr;
		FBodyInstance* Sphere = nullptr;
		FBodyInstance* Kinematic = nullptr;

		FTestScene()
		{
			FPhysSceneInitParams Params;
			Params.DebugName = "DebugDrawTest";
			Params.BackendName = "Null";
			Scene = std::make_unique<FPhysScene>(Params);
			FBodySetup GroundSetup;
			GroundSetup.AggGeom.BoxElems.emplace_back(2000.0, 2000.0, 100.0);
			FBodySetup BoxSetup;
			BoxSetup.AggGeom.BoxElems.emplace_back(100.0, 60.0, 40.0);   // full extents
			FBodySetup CapsuleSetup;
			CapsuleSetup.AggGeom.SphylElems.emplace_back(20.0, 60.0);   // radius 20, segment 60 -> half length 30
			FBodySetup SphereSetup;
			SphereSetup.AggGeom.SphereElems.emplace_back(25.0);
			const FBodySetup* GroundPtr = Scene->CreateBodySetup(std::move(GroundSetup));
			const FBodySetup* BoxPtr = Scene->CreateBodySetup(std::move(BoxSetup));
			const FBodySetup* CapsulePtr = Scene->CreateBodySetup(std::move(CapsuleSetup));
			const FBodySetup* SpherePtr = Scene->CreateBodySetup(std::move(SphereSetup));
			FBodyInstance Static;
			Static.bStatic = true;
			Static.BodyName = "Ground";
			Ground = Scene->SpawnBody(Static, GroundPtr, FTransform(FVector(0.0, 0.0, -50.0)));
			FBodyInstance Dynamic;
			Dynamic.bSimulatePhysics = true;
			Dynamic.bEnableGravity = false;
			Dynamic.BodyName = "Box";
			Box = Scene->SpawnBody(Dynamic, BoxPtr, FTransform(FRotator(0.0, 30.0, 0.0), FVector(0.0, 0.0, 20.0)));
			Dynamic.BodyName = "Capsule";
			Capsule = Scene->SpawnBody(Dynamic, CapsulePtr, FTransform(FRotator(90.0, 0.0, 0.0), FVector(200.0, 0.0, 20.0)));
			Dynamic.BodyName = "Sphere";
			Sphere = Scene->SpawnBody(Dynamic, SpherePtr, FTransform(FVector(-200.0, 0.0, 25.0)));
			FBodyInstance Kin;
			Kin.BodyName = "Kinematic";
			Kinematic = Scene->SpawnBody(Kin, BoxPtr, FTransform(FVector(0.0, 300.0, 100.0)));
			FConstraintInstance Joint;
			Joint.Pos1 = FVector(50.0, 0.0, 0.0);
			Joint.Pos2 = FVector(-30.0, 0.0, 0.0);
			Scene->SpawnConstraint(Joint, Box, Capsule);
			Scene->FlushDeferredActors();
		}
	};

	PtContactPoint MakeContact(const uint32 Body0, const uint32 Body1, const FVector& OnPlane, const double Phi, const uint32 Flags)
	{
		PtContactPoint Contact = {};
		Contact.Body0 = Body0;
		Contact.Body1 = Body1;
		Contact.PointOn1 = PtVec3{ OnPlane.X, OnPlane.Y, OnPlane.Z };
		Contact.PointOn0 = PtVec3{ OnPlane.X, OnPlane.Y, OnPlane.Z + Phi };
		Contact.Normal = PtVec3{ 0.0, 0.0, 1.0 };
		Contact.Phi = Phi;
		Contact.NormalImpulse = std::nan("");
		Contact.FrictionImpulse = PtVec3{ std::nan(""), std::nan(""), std::nan("") };
		Contact.Flags = Flags;
		return Contact;
	}

	// The host snapshot of the test scene, plus synthetic contacts and a joint (the Null backend has neither).
	FPhysicsDebugSnapshot MakeSyntheticSnapshot(const FTestScene& Test)
	{
		FPhysicsDebugSnapshot Snapshot;
		DebugDraw::CaptureSnapshot(*Test.Scene, PT_SNAP_PARTICLES | PT_SNAP_CONTACTS | PT_SNAP_JOINTS, Snapshot);
		Snapshot.Contacts.push_back(MakeContact(1, 0, FVector(10.0, 0.0, 0.0), -1.0, PT_CONTACT_ACTIVE));
		Snapshot.Contacts.push_back(MakeContact(1, 0, FVector(-10.0, 0.0, 0.0), 0.5, PT_CONTACT_SPECULATIVE));
		Snapshot.Contacts.push_back(MakeContact(2, 1, FVector(100.0, 0.0, 20.0), -0.2, PT_CONTACT_ACTIVE | PT_CONTACT_STATIC_FRICTION));
		PtJointSnapshot Joint = {};
		Joint.JointIndex = 0;
		Joint.Frame1World = PtTransform{ PtQuat{ 0.0, 0.0, 0.0, 1.0 }, PtVec3{ 50.0, 0.0, 20.0 } };
		Joint.Frame2World = PtTransform{ PtQuat{ 0.0, 0.0, 0.0, 1.0 }, PtVec3{ 170.0, 0.0, 20.0 } };
		Snapshot.Joints.push_back(Joint);
		FIslandBuilder::Build(Snapshot, Test.Scene->GetConstraints());
		return Snapshot;
	}

	bool SameCommand(const FLatentDrawCommand& A, const FLatentDrawCommand& B)
	{
		return A.Type == B.Type && A.Color == B.Color && A.LineStart == B.LineStart && A.LineEnd == B.LineEnd && A.Center == B.Center && A.Extent == B.Extent
			&& A.Radius == B.Radius && A.HalfHeight == B.HalfHeight && A.Text == B.Text && A.Thickness == B.Thickness;
	}

	std::vector<FLatentDrawCommand> Run(const std::function<void(FDebugDrawQueue&)>& Drawer)
	{
		FDebugDrawQueue Queue;
		Queue.SetMaxCost(0);
		Queue.SetRegionOfInterest(FVector::ZeroVector, 0.0f);
		Drawer(Queue);
		std::vector<FLatentDrawCommand> Commands;
		Queue.ExtractAllElements(Commands);
		return Commands;
	}

	// IDebugDrawRenderer that records the calls.
	struct FRecordingRenderer final : IDebugDrawRenderer
	{
		struct FCall
		{
			std::string Name;
			FVector A;
			FVector B;
			FVector Size;
			float Radius = 0.0f;
			float HalfHeight = 0.0f;
			float Thickness = 0.0f;
			FColor Color;
			FMatrix Axes;
		};
		std::vector<FCall> Calls;
		bool IsServer() const override { return false; }
		FSphere GetDrawRegion() const override { return FSphere(); }
		void RenderPoint(const FVector& P, const FColor& C, const float Size, float) override { Calls.push_back({ "Point", P, {}, {}, Size, 0.0f, 0.0f, C, {} }); }
		void RenderLine(const FVector& A, const FVector& B, const FColor& C, const float T, float) override { Calls.push_back({ "Line", A, B, {}, 0.0f, 0.0f, T, C, {} }); }
		void RenderArrow(const FVector& A, const FVector& B, const float S, const FColor& C, const float T, float) override
		{
			Calls.push_back({ "Arrow", A, B, FVector(S), 0.0f, 0.0f, T, C, {} });
		}
		void RenderCircle(const FVector& Center, const FMatrix& Axes, const float R, const FColor& C, const float T, float) override
		{
			Calls.push_back({ "Circle", Center, {}, {}, R, 0.0f, T, C, Axes });
		}
		void RenderSphere(const FVector& Center, const float R, const FColor& C, const float T, float) override { Calls.push_back({ "Sphere", Center, {}, {}, R, 0.0f, T, C, {} }); }
		void RenderCapsule(const FVector& Center, const FQuat&, const float H, const float R, const FColor& C, const float T, float) override
		{
			Calls.push_back({ "Capsule", Center, {}, {}, R, H, T, C, {} });
		}
		void RenderBox(const FVector& P, const FQuat&, const FVector& Size, const FColor& C, const float T, float) override { Calls.push_back({ "Box", P, {}, Size, 0.0f, 0.0f, T, C, {} }); }
		void RenderTriangle(const FVector& A, const FVector& B, const FVector&, const FColor& C, const float T, float) override { Calls.push_back({ "Triangle", A, B, {}, 0.0f, 0.0f, T, C, {} }); }
		void RenderString(const FVector& P, std::string_view, const FColor& C, const float, bool, float) override { Calls.push_back({ "String", P, {}, {}, 0.0f, 0.0f, 0.0f, C, {} }); }
	};
}

// 03 M4: "선분 수 12 / 128 / 68 (단위 테스트)" - and the circle of the ChaosDD renderer (8).
TEST_CASE("PhysicsTest.DebugDraw.WireMeshSegmentCounts")
{
	std::vector<DebugDraw::FWireVertex> Box;
	std::vector<DebugDraw::FWireVertex> Sphere;
	std::vector<DebugDraw::FWireVertex> Capsule;
	std::vector<DebugDraw::FWireVertex> Circle;
	DebugDraw::AppendUnitBox(Box);
	DebugDraw::AppendUnitSphere(DebugDraw::GSphereSegments, Sphere);
	DebugDraw::AppendUnitCapsule(DebugDraw::GCapsuleSides, Capsule);
	DebugDraw::AppendUnitCircle(DebugDraw::GCircleSegments, Circle);
	CHECK(DebugDraw::CountSegments(Box) == 12);
	CHECK(DebugDraw::CountSegments(Sphere) == 128);
	CHECK(DebugDraw::CountSegments(Capsule) == 68);
	CHECK(DebugDraw::CountSegments(Circle) == 8);
	MESSAGE("box " << DebugDraw::CountSegments(Box) << ", sphere(8) " << DebugDraw::CountSegments(Sphere) << ", capsule(16) " << DebugDraw::CountSegments(Capsule)
		<< ", circle(8) " << DebugDraw::CountSegments(Circle));
	// Every vertex lies on its unit shape: box corners at +-1, sphere and circle radius 1, capsule ring / caps radius 1.
	for (const DebugDraw::FWireVertex& V : Box)
	{
		CHECK((std::abs(V.X) == 1.0f && std::abs(V.Y) == 1.0f && std::abs(V.Z) == 1.0f));
	}
	for (const DebugDraw::FWireVertex& V : Sphere)
	{
		CHECK(std::abs(std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z) - 1.0f) < 1e-5f);
	}
	// The sphere grid (LineBatchComponent DrawSphere's topology): cell (band 0, step 0) emits the parallel at the +Z pole
	// (zero length) and then the meridian to polar 45 deg at azimuth 0; cell (band 1, step 0) starts there and its parallel
	// goes to azimuth 45 deg. The last band's meridians end back at the +Z pole (polar 2 pi).
	const float Half = std::sqrt(0.5f);
	const auto Near = [](const DebugDraw::FWireVertex& V, const float X, const float Y, const float Z)
	{
		return std::abs(V.X - X) < 1e-5f && std::abs(V.Y - Y) < 1e-5f && std::abs(V.Z - Z) < 1e-5f;
	};
	CHECK(Near(Sphere[0], 0.0f, 0.0f, 1.0f));
	CHECK(Near(Sphere[1], 0.0f, 0.0f, 1.0f));
	CHECK(Near(Sphere[2], 0.0f, 0.0f, 1.0f));
	CHECK(Near(Sphere[3], Half, 0.0f, Half));
	CHECK(Near(Sphere[32], Half, 0.0f, Half));     // segment 16 = cell (1, 0), parallel
	CHECK(Near(Sphere[33], 0.5f, 0.5f, Half));
	CHECK(Near(Sphere[Sphere.size() - 1], 0.0f, 0.0f, 1.0f));
	for (const DebugDraw::FWireVertex& V : Capsule)
	{
		CHECK(std::abs(std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z) - 1.0f) < 1e-5f);
		CHECK(std::abs(V.W) == 1.0f);   // every capsule vertex belongs to the top or the bottom end
	}
	for (const DebugDraw::FWireVertex& V : Circle)
	{
		CHECK(V.X == 0.0f);   // the Y-Z plane
	}
}

// 03 M4: "상태색과 bounds 색이 위 표와 같다", the island palette and Black for sleeping islands.
TEST_CASE("PhysicsTest.DebugDraw.StateAndIslandColors")
{
	CHECK(DebugDraw::GetShapeColorByState(EObjectStateType::Dynamic) == FColor(255, 255, 0));
	CHECK(DebugDraw::GetShapeColorByState(EObjectStateType::Sleeping) == FColor(128, 128, 128));
	CHECK(DebugDraw::GetShapeColorByState(EObjectStateType::Kinematic) == FColor(0, 128, 255));
	CHECK(DebugDraw::GetShapeColorByState(EObjectStateType::Static) == FColor(255, 0, 0));
	CHECK(DebugDraw::GetBoundsColorByState(EObjectStateType::Dynamic) == FColor(128, 128, 0));
	CHECK(DebugDraw::GetBoundsColorByState(EObjectStateType::Sleeping) == FColor(64, 64, 64));
	CHECK(DebugDraw::GetBoundsColorByState(EObjectStateType::Kinematic) == FColor(0, 64, 128));
	CHECK(DebugDraw::GetBoundsColorByState(EObjectStateType::Static) == FColor(128, 0, 0));
	CHECK(DebugDraw::GetIslandColor(INDEX_NONE, true) == FColor::White);
	CHECK(DebugDraw::GetIslandColor(3, false) == FColor::Black);
	const FColor Palette[10] = { FColor::Red, FColor::Orange, FColor::Yellow, FColor::Green, FColor::Emerald, FColor::Cyan, FColor::Turquoise, FColor::Blue,
		FColor::Magenta, FColor::Purple };
	for (int32 Index = 0; Index < 20; ++Index)
	{
		CHECK(DebugDraw::GetIslandColor(Index, true) == Palette[Index % 10]);
	}
	CHECK(FColor::Orange == FColor(243, 156, 18));
	CHECK(FColor::Purple == FColor(169, 7, 228));
}

// 03 M4: "명령 스트림 단위 테스트: Draw 호출 순서가 위 9단계와 같다" (PBDRigidsSolver.cpp:3417-3546), joints from a synthetic
// snapshot ("조인트 드로는 합성 스냅샷으로 검증한다").
TEST_CASE("PhysicsTest.DebugDraw.DrawOrderMatchesPostTickDebugDraw")
{
	const FTestScene Test;
	const FPhysicsDebugSnapshot Snapshot = MakeSyntheticSnapshot(Test);
	std::vector<int32> ParticleOfBody;
	DebugDraw::BuildParticleLookup(Snapshot, ParticleOfBody);
	FDebugDrawSettings Settings;
	FDebugDrawFlags All;
	All.bDrawShapes = All.bDrawCollisions = All.bDrawMass = All.bDrawBounds = All.bDrawTransforms = true;
	All.bDrawIslands = All.bDrawSleepState = All.bDrawCollidingShapes = All.bDrawJoints = true;
	All.MaxLines = 0;
	All.Radius = 0.0f;
	const DebugDraw::FDrawContext Context{ *Test.Scene, Snapshot, Settings, All, ParticleOfBody };
	using FDrawer = void (*)(FDebugDrawQueue&, const DebugDraw::FDrawContext&);
	const std::pair<const char*, FDrawer> Stages[] = {
		{ "Shapes", &DebugDraw::DrawParticleShapes },
		{ "Collisions", &DebugDraw::DrawCollisions },
		{ "Mass", &DebugDraw::DrawParticleMass },
		{ "Bounds", &DebugDraw::DrawParticleBounds },
		{ "Transforms", &DebugDraw::DrawParticleTransforms },
		{ "ConstraintGraph", &DebugDraw::DrawConstraintGraph },
		{ "SleepState", &DebugDraw::DrawSleepState },
		{ "CollidingShapes", &DebugDraw::DrawCollidingShapes },
		{ "Joints", &DebugDraw::DrawJointConstraints },
	};
	std::vector<FLatentDrawCommand> Expected;
	for (const auto& [Name, Drawer] : Stages)
	{
		const std::vector<FLatentDrawCommand> StageCommands = Run([&](FDebugDrawQueue& Queue) { Drawer(Queue, Context); });
		const std::string StageName(Name);
		CAPTURE(StageName);
		CHECK_FALSE(StageCommands.empty());   // every stage contributes, so the order is observable
		MESSAGE(StageName << ": " << StageCommands.size() << " commands");
		Expected.insert(Expected.end(), StageCommands.begin(), StageCommands.end());
	}
	const std::vector<FLatentDrawCommand> Actual = Run([&](FDebugDrawQueue& Queue) { DebugDraw::DrawAll(Queue, Context); });
	REQUIRE(Actual.size() == Expected.size());
	for (size_t Index = 0; Index < Actual.size(); ++Index)
	{
		CAPTURE(Index);
		CHECK(SameCommand(Actual[Index], Expected[Index]));
	}
	// Shapes: statics, then kinematics, then dynamics (FPBDRigidsSolver::DebugDrawShapes).
	const std::vector<FLatentDrawCommand> Shapes = Run([&](FDebugDrawQueue& Queue) { DebugDraw::DrawParticleShapes(Queue, Context); });
	REQUIRE(Shapes.size() == 5u);
	CHECK(Shapes[0].Color == FColor(255, 0, 0));     // ground (static)
	CHECK(Shapes[1].Color == FColor(0, 128, 255));   // kinematic
	CHECK(Shapes[2].Color == FColor(255, 255, 0));   // box (dynamic)
	CHECK(Shapes[0].Type == EDrawType::Box);         // the ground is drawn as a box (Chaos draws no plane)
	// Disabled debug draw: nothing at all.
	FDebugDrawFlags Off = All;
	Off.bEnabled = false;
	const DebugDraw::FDrawContext OffContext{ *Test.Scene, Snapshot, Settings, Off, ParticleOfBody };
	CHECK(Run([&](FDebugDrawQueue& Queue) { DebugDraw::DrawAll(Queue, OffContext); }).empty());
}

// 03 M4: "ABI HalfLength → DrawDebugCapsule HalfHeight = HalfLength + R. 박스는 half extent 그대로 넘긴다."
TEST_CASE("PhysicsTest.DebugDraw.CapsuleAndBoxConversion")
{
	const FTestScene Test;
	FPhysicsDebugSnapshot Snapshot;
	DebugDraw::CaptureSnapshot(*Test.Scene, PT_SNAP_PARTICLES, Snapshot);
	std::vector<int32> ParticleOfBody;
	DebugDraw::BuildParticleLookup(Snapshot, ParticleOfBody);
	const FDebugDrawSettings Settings;
	FDebugDrawFlags Flags;
	Flags.Radius = 0.0f;
	const DebugDraw::FDrawContext Context{ *Test.Scene, Snapshot, Settings, Flags, ParticleOfBody };
	const std::vector<FLatentDrawCommand> Commands = Run([&](FDebugDrawQueue& Queue) { DebugDraw::DrawParticleShapes(Queue, Context); });
	const auto Capsule = std::find_if(Commands.begin(), Commands.end(), [](const FLatentDrawCommand& C) { return C.Type == EDrawType::Capsule; });
	REQUIRE(Capsule != Commands.end());
	CHECK(Capsule->Radius == doctest::Approx(20.0));
	CHECK(Capsule->HalfHeight == doctest::Approx(30.0 + 20.0));   // ABI HalfLength 30 + Radius 20
	// The capsule's long axis is its local Z: pitched 90 degrees it lies along world X.
	CHECK(std::abs(Capsule->Rotation.GetAxisZ().X) == doctest::Approx(1.0).epsilon(1e-9));
	const auto Box = std::find_if(Commands.begin(), Commands.end(), [](const FLatentDrawCommand& C) { return C.Type == EDrawType::Box && C.Color == FColor(255, 255, 0); });
	REQUIRE(Box != Commands.end());
	CHECK(Box->Extent == FVector(50.0, 30.0, 20.0));   // FKBoxElem full extents 100 x 60 x 40 -> half extents
	const auto Sphere = std::find_if(Commands.begin(), Commands.end(), [](const FLatentDrawCommand& C) { return C.Type == EDrawType::Sphere; });
	REQUIRE(Sphere != Commands.end());
	CHECK(Sphere->Radius == doctest::Approx(25.0));
	CHECK(Sphere->Segments == 8);
	// VisitCommands hands the same values to the renderer (RenderCapsule HalfHeight, RenderBox Size = half extent).
	FRecordingRenderer Renderer;
	VisitCommands(Commands, Renderer);
	const auto RenderedCapsule = std::find_if(Renderer.Calls.begin(), Renderer.Calls.end(), [](const FRecordingRenderer::FCall& C) { return C.Name == "Capsule"; });
	REQUIRE(RenderedCapsule != Renderer.Calls.end());
	CHECK(RenderedCapsule->HalfHeight == doctest::Approx(50.0f));
	CHECK(RenderedCapsule->Radius == doctest::Approx(20.0f));
	const auto RenderedBox = std::find_if(Renderer.Calls.begin(), Renderer.Calls.end(),
		[](const FRecordingRenderer::FCall& C) { return C.Name == "Box" && C.Color == FColor(255, 255, 0); });
	REQUIRE(RenderedBox != Renderer.Calls.end());
	CHECK(RenderedBox->Size == FVector(50.0, 30.0, 20.0));
	// Shape lines use ShapeThicknesScale x LineThickness = 1 cm.
	CHECK(RenderedBox->Thickness == doctest::Approx(1.0f));
}

// The circle frame and the arrowhead side axis (M4 fix round 2): a circle command reaches the renderer with an orthonormal
// right-handed frame whose Y is the first in-plane axis, also for parallel or zero axes (never zero or NaN axes); and
// FVector::GetPerpendicularUnit opens the arrowhead in the plane of UE's line batcher arrow (across world Z, or across world X
// for a mostly vertical shaft; LineBatchComponent.cpp:361-383 [V]).
TEST_CASE("PhysicsTest.DebugDraw.CircleFrameAndArrowSide")
{
	const std::vector<FLatentDrawCommand> Commands = Run([](FDebugDrawQueue& Queue)
	{
		const auto Circle = [&Queue](const FVector& YAxis, const FVector& ZAxis)
		{
			Queue.DrawDebugCircle(FVector::ZeroVector, 10.0, 12, FColor::Red, false, -1.0f, 0, 1.0f, YAxis, ZAxis, false);
		};
		Circle(FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0));   // the usual case
		Circle(FVector(0.0, 0.0, 1.0), FVector(0.0, 0.0, 2.0));   // parallel axes
		Circle(FVector(0.0, 0.0, 0.0), FVector(1.0, 0.0, 0.0));   // a zero first axis
		Circle(FVector(0.0, 0.0, 3.0), FVector(0.0, 0.0, 0.0));   // a zero second axis
	});
	FRecordingRenderer Renderer;
	VisitCommands(Commands, Renderer);
	REQUIRE(Renderer.Calls.size() == 4u);
	for (const FRecordingRenderer::FCall& Call : Renderer.Calls)
	{
		const FVector X = Call.Axes.GetScaledAxis(EAxis::X);
		const FVector Y = Call.Axes.GetScaledAxis(EAxis::Y);
		const FVector Z = Call.Axes.GetScaledAxis(EAxis::Z);
		CHECK_FALSE((X.ContainsNaN() || Y.ContainsNaN() || Z.ContainsNaN()));
		CHECK(X.Size() == doctest::Approx(1.0));
		CHECK(Y.Size() == doctest::Approx(1.0));
		CHECK(Z.Size() == doctest::Approx(1.0));
		CHECK(std::abs(X | Y) < 1e-12);
		CHECK(std::abs(X | Z) < 1e-12);
		CHECK(std::abs(Y | Z) < 1e-12);
		CHECK((X - (Y ^ Z)).Size() < 1e-12);
	}
	CHECK(Renderer.Calls[0].Axes.GetScaledAxis(EAxis::X).Equals(FVector(1.0, 0.0, 0.0), 1e-12));
	CHECK(Renderer.Calls[1].Axes.GetScaledAxis(EAxis::Y).Equals(FVector(0.0, 0.0, 1.0), 1e-12));
	CHECK(Renderer.Calls[2].Axes.GetScaledAxis(EAxis::Y).Equals(FVector(0.0, 1.0, 0.0), 1e-12));   // world Y fallback
	CHECK(Renderer.Calls[2].Axes.GetScaledAxis(EAxis::Z).Equals(FVector(1.0, 0.0, 0.0), 1e-12));
	CHECK(Renderer.Calls[3].Axes.GetScaledAxis(EAxis::Y).Equals(FVector(0.0, 0.0, 1.0), 1e-12));

	// The arrowhead side: UE's head plane (up to the sign, both head strokes are drawn).
	const auto SameLine = [](const FVector& A, const FVector& B) { return std::abs(std::abs(A | B) - 1.0) < 1e-12; };
	CHECK(SameLine(FVector(1.0, 0.0, 0.0).GetPerpendicularUnit(), FVector(0.0, 1.0, 0.0)));    // horizontal: across world Z
	CHECK(SameLine(FVector(0.0, 0.0, 1.0).GetPerpendicularUnit(), FVector(0.0, 1.0, 0.0)));    // vertical: across world X
	CHECK(SameLine(FVector(0.0, 0.0, -1.0).GetPerpendicularUnit(), FVector(0.0, 1.0, 0.0)));
	CHECK(SameLine(FVector(0.6, 0.0, 0.8).GetPerpendicularUnit(), FVector(0.0, 1.0, 0.0)));    // mostly vertical
	CHECK(SameLine(FVector(0.8, 0.0, 0.6).GetPerpendicularUnit(), FVector(0.0, 1.0, 0.0)));    // mostly horizontal
	CHECK(SameLine(FVector(0.0, 0.6, 0.8).GetPerpendicularUnit(), FVector(0.0, 0.8, -0.6)));   // across world X, tilted
	CHECK(FVector::ZeroVector.GetPerpendicularUnit().IsZero());
	FRandomStream Random(7);
	for (int32 Sample = 0; Sample < 1000; ++Sample)
	{
		const FVector Direction = FVector(Random.FRandRange(-1.0, 1.0), Random.FRandRange(-1.0, 1.0), Random.FRandRange(-1.0, 1.0)).GetSafeNormal();
		if (Direction.IsZero())
		{
			continue;
		}
		const FVector Side = Direction.GetPerpendicularUnit();
		CHECK(Side.Size() == doctest::Approx(1.0));
		CHECK(std::abs(Side | Direction) < 1e-12);
	}
}

// 03 M4: "접촉 원 반경 6 / 3, normal 길이 30, 비활성 접촉은 ×0.75", "COM 축 30, 화살표 10".
TEST_CASE("PhysicsTest.DebugDraw.ContactsAndTransforms")
{
	const FTestScene Test;
	const FPhysicsDebugSnapshot Snapshot = MakeSyntheticSnapshot(Test);
	std::vector<int32> ParticleOfBody;
	DebugDraw::BuildParticleLookup(Snapshot, ParticleOfBody);
	const FDebugDrawSettings Settings;
	FDebugDrawFlags Flags;
	Flags.Radius = 0.0f;
	const DebugDraw::FDrawContext Context{ *Test.Scene, Snapshot, Settings, Flags, ParticleOfBody };
	const std::vector<FLatentDrawCommand> Collisions = Run([&](FDebugDrawQueue& Queue) { DebugDraw::DrawCollisions(Queue, Context); });
	REQUIRE(Collisions.size() == 9u);   // 3 points x (plane disc, point disc, normal)
	// Point 0: active - red discs r = 6 (plane, on PointOn1) and 3 (point), red normal of 30 cm along +Z.
	CHECK(Collisions[0].Type == EDrawType::Circle);
	CHECK(Collisions[0].Radius == doctest::Approx(6.0));
	CHECK(Collisions[0].Center == FVector(10.0, 0.0, 0.0));
	CHECK(Collisions[0].Color == FColor(250, 0, 0));
	CHECK(Collisions[1].Radius == doctest::Approx(3.0));
	CHECK(Collisions[1].Center == FVector(10.0, 0.0, -1.0));
	CHECK(Collisions[2].Type == EDrawType::Line);
	CHECK((Collisions[2].LineEnd - Collisions[2].LineStart).Z == doctest::Approx(30.0));
	CHECK(Collisions[2].Color == FColor(250, 0, 0));
	// The disc plane is perpendicular to the normal (its Y and Z axes span the contact plane).
	CHECK(std::abs(Collisions[0].YAxis | FVector(0.0, 0.0, 1.0)) < 1e-12);
	CHECK(std::abs(Collisions[0].ZAxis | FVector(0.0, 0.0, 1.0)) < 1e-12);
	// Point 1: inactive (speculative) - grey, 0.75 x 30.
	CHECK(Collisions[3].Color == FColor(150, 150, 150));
	CHECK((Collisions[5].LineEnd - Collisions[5].LineStart).Z == doctest::Approx(22.5));
	CHECK(Collisions[5].Color == FColor(150, 150, 150));
	// Point 2: active in the static friction cone - yellow-green discs, red normal.
	CHECK(Collisions[6].Color == FColor(150, 200, 0));
	CHECK(Collisions[8].Color == FColor(250, 0, 0));
	// p.Chaos.DebugDraw.ShowInactiveContacts 0 drops point 1.
	FDebugDrawFlags ActiveOnly = Flags;
	ActiveOnly.bShowInactiveContacts = false;
	const DebugDraw::FDrawContext ActiveContext{ *Test.Scene, Snapshot, Settings, ActiveOnly, ParticleOfBody };
	CHECK(Run([&](FDebugDrawQueue& Queue) { DebugDraw::DrawCollisions(Queue, ActiveContext); }).size() == 6u);

	const std::vector<FLatentDrawCommand> Transforms = Run([&](FDebugDrawQueue& Queue) { DebugDraw::DrawParticleTransforms(Queue, Context); });
	// Per particle: 3 arrows, a point, a line (+ the inertia box of dynamic bodies, InertiaScale 1).
	REQUIRE(Transforms.size() >= 5u);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const FLatentDrawCommand& Arrow = Transforms[static_cast<size_t>(Axis)];
		CHECK(Arrow.Type == EDrawType::DirectionalArrow);
		CHECK((Arrow.LineEnd - Arrow.LineStart).Size() == doctest::Approx(30.0));   // BodyAxisLen
		CHECK(Arrow.ArrowSize == doctest::Approx(10.0f));                           // ArrowSize
	}
	CHECK(Transforms[0].Color == FColor::Red);
	CHECK(Transforms[1].Color == FColor::Green);
	CHECK(Transforms[2].Color == FColor::Blue);
	CHECK(Transforms[3].Type == EDrawType::Point);
	CHECK(Transforms[3].Color == FColor::Black);
	CHECK(Transforms[3].Thickness == doctest::Approx(5.0f));   // PointSize
	CHECK(Transforms[4].Color == FColor(64, 64, 64));
	// Arrow heads in the renderer: two head lines of length sqrt(2) * sqrt(ArrowSize) end at the tip.
	FRecordingRenderer Renderer;
	VisitCommands(Transforms, Renderer);
	CHECK(Renderer.Calls[0].Name == "Arrow");
	CHECK(Renderer.Calls[0].Size.X == doctest::Approx(10.0));
}

TEST_CASE("PhysicsTest.DebugDraw.IslandBuilder")
{
	// Bodies: 0 static, 1..5 dynamic, 6 kinematic. Edges: 1-0 (Phi -1), 2-1 (-0.5), 3-0 speculative (+1), 4-6 (kinematic),
	// 5-2 (-0.1). Expected: {1, 2, 5} -> island 1, 3 isolated (speculative only), 4 alone in its island (touches a
	// kinematic body only), statics / kinematics none.
	FPhysicsDebugSnapshot Snapshot;
	const uint8 States[7] = { 3, 4, 4, 4, 1, 4, 2 };
	for (uint32 Index = 0; Index < 7; ++Index)
	{
		PtParticleSnapshot Particle = {};
		Particle.BodyIndex = Index;
		Particle.ObjectState = States[Index];
		Particle.BoundsMin = PtVec3{ Index * 100.0, 0.0, 0.0 };
		Particle.BoundsMax = PtVec3{ Index * 100.0 + 50.0, 50.0, 50.0 };
		Snapshot.Particles.push_back(Particle);
	}
	std::vector<PtContactPoint> Contacts = { MakeContact(1, 0, FVector(), -1.0, PT_CONTACT_ACTIVE), MakeContact(2, 1, FVector(), -0.5, PT_CONTACT_ACTIVE),
		MakeContact(3, 0, FVector(), 1.0, PT_CONTACT_SPECULATIVE), MakeContact(4, 6, FVector(), -0.3, PT_CONTACT_ACTIVE),
		MakeContact(5, 2, FVector(), -0.1, PT_CONTACT_ACTIVE) };
	Snapshot.Contacts = Contacts;
	FIslandBuilder::Build(Snapshot, {});
	const std::vector<int32> Expected = { INDEX_NONE, 1, 1, INDEX_NONE, 4, 1, INDEX_NONE };
	CHECK(Snapshot.HarnessIslandIndex == Expected);
	// Deterministic: the contact order does not matter.
	std::reverse(Snapshot.Contacts.begin(), Snapshot.Contacts.end());
	FIslandBuilder::Build(Snapshot, {});
	CHECK(Snapshot.HarnessIslandIndex == Expected);
	std::vector<FHarnessIsland> Islands;
	FIslandBuilder::GatherIslands(Snapshot, Islands);
	REQUIRE(Islands.size() == 2u);
	CHECK(Islands[0].Id == 1);
	CHECK(Islands[0].NumParticles == 3);
	CHECK(Islands[0].bAwake);
	CHECK(Islands[1].Id == 4);
	CHECK_FALSE(Islands[1].bAwake);   // body 4 is asleep: its island draws Black
	CHECK(DebugDraw::GetIslandColor(1, Islands[1].bAwake) == FColor::Black);
	CHECK(Islands[0].Bounds.Min.X == doctest::Approx(100.0));
	CHECK(Islands[0].Bounds.Max.X == doctest::Approx(550.0));
	// A joint is an edge too: the test scene's joint between bodies 1 and 2 makes them one island without any contact.
	const FTestScene Test;   // only for a constraint whose bodies have actors 1 and 2
	FConstraintInstance* Joint = Test.Scene->GetConstraints()[0];
	REQUIRE(Joint->Body1->ActorHandle.Index == 1u);
	REQUIRE(Joint->Body2->ActorHandle.Index == 2u);
	FPhysicsDebugSnapshot JointOnly;
	JointOnly.Particles = Snapshot.Particles;
	FIslandBuilder::Build(JointOnly, Test.Scene->GetConstraints());
	CHECK(JointOnly.HarnessIslandIndex[1] == 1);
	CHECK(JointOnly.HarnessIslandIndex[2] == 1);
	CHECK(JointOnly.HarnessIslandIndex[3] == INDEX_NONE);
	// M5: a point the solver pushed on is an edge whatever its Phi (a resting PhysX point is Phi ~ +0 with an impulse); a
	// speculative point without one is not.
	FPhysicsDebugSnapshot Resting;
	Resting.Particles = Snapshot.Particles;
	Resting.Contacts = { MakeContact(3, 0, FVector(), 1.0e-5, PT_CONTACT_ACTIVE), MakeContact(5, 3, FVector(), 0.5, PT_CONTACT_SPECULATIVE) };
	FIslandBuilder::Build(Resting, {});
	CHECK(Resting.HarnessIslandIndex[3] == 3);
	CHECK(Resting.HarnessIslandIndex[5] == INDEX_NONE);
}

// M5 (M4 carry-over (2)): a backend-reported BodyIndex never sizes an array. PT_INDEX_NONE and indices at or beyond the
// snapshot's body bound are skipped by BuildParticleLookup, FIslandBuilder::Build and GatherIslands (the host capture drops
// them before, FPhysicsBackendScene::CaptureDebugSnapshot; a hand-made snapshot without NumBodies ignores PT_INDEX_NONE).
TEST_CASE("PhysicsTest.DebugDraw.SnapshotBodyIndexBounds")
{
	FPhysicsDebugSnapshot Snapshot;
	const uint32 Indices[] = { 0, 1, PT_INDEX_NONE, 2, 0x7FFFFFFFu };
	const uint8 States[] = { 3, 4, 4, 4, 4 };
	for (size_t Slot = 0; Slot < std::size(Indices); ++Slot)
	{
		PtParticleSnapshot Particle = {};
		Particle.BodyIndex = Indices[Slot];
		Particle.ObjectState = States[Slot];
		Snapshot.Particles.push_back(Particle);
	}
	Snapshot.Contacts = { MakeContact(1, 0, FVector(), -1.0, PT_CONTACT_ACTIVE), MakeContact(2, 1, FVector(), -1.0, PT_CONTACT_ACTIVE),
		MakeContact(0x7FFFFFFFu, 1, FVector(), -1.0, PT_CONTACT_ACTIVE), MakeContact(PT_INDEX_NONE, 2, FVector(), -1.0, PT_CONTACT_ACTIVE) };
	Snapshot.NumBodies = 3;   // as the host capture sets it
	CHECK(GetBodyIndexBound(Snapshot) == 3u);
	std::vector<int32> ParticleOfBody;
	DebugDraw::BuildParticleLookup(Snapshot, ParticleOfBody);
	CHECK(ParticleOfBody == std::vector<int32>{ 0, 1, 3 });
	FIslandBuilder::Build(Snapshot, {});
	CHECK(Snapshot.HarnessIslandIndex == std::vector<int32>{ INDEX_NONE, 1, INDEX_NONE, 1, INDEX_NONE });
	std::vector<FHarnessIsland> Islands;
	FIslandBuilder::GatherIslands(Snapshot, Islands);
	REQUIRE(Islands.size() == 1u);
	CHECK(Islands[0].Id == 1);
	CHECK(Islands[0].NumParticles == 2);
	// Without NumBodies (a hand-made snapshot) the bound comes from the particles, PT_INDEX_NONE excluded; a garbage index far
	// beyond the particle count (0x7FFFFFFF above, 0x7FFFFFFE here) is skipped too, never sized (M5 round 0).
	Snapshot.NumBodies = 0;
	CHECK(GetBodyIndexBound(Snapshot) == 3u);
	Snapshot.Particles[4].BodyIndex = 0x7FFFFFFEu;
	CHECK(GetBodyIndexBound(Snapshot) == 3u);
	DebugDraw::BuildParticleLookup(Snapshot, ParticleOfBody);
	CHECK(ParticleOfBody == std::vector<int32>{ 0, 1, 3 });
	Snapshot.Particles[4].BodyIndex = 2u * 5u + static_cast<uint32>(GHandMadeBodyIndexSlack) - 1u;   // the largest plausible index
	CHECK(GetBodyIndexBound(Snapshot) == 2u * 5u + static_cast<uint32>(GHandMadeBodyIndexSlack));
	Snapshot.Particles[4].BodyIndex = 4;
	CHECK(GetBodyIndexBound(Snapshot) == 5u);
	DebugDraw::BuildParticleLookup(Snapshot, ParticleOfBody);
	CHECK(ParticleOfBody == std::vector<int32>{ 0, 1, 3, INDEX_NONE, 4 });
	FIslandBuilder::Build(Snapshot, {});
	CHECK(Snapshot.HarnessIslandIndex[2] == INDEX_NONE);
}

// Chaos::FDebugDrawQueue cost and FChaosDDFrame region / budget (p.Chaos.DebugDraw.MaxLines 20000, .Radius 3000 cm).
TEST_CASE("PhysicsTest.DebugDraw.QueueBudgetAndRegion")
{
	FDebugDrawQueue Queue;
	Queue.SetMaxCost(30);
	Queue.SetRegionOfInterest(FVector::ZeroVector, 1000.0f);
	Queue.DrawDebugBox(FVector(0.0, 0.0, 0.0), FVector(1.0), FQuat::Identity, FColor::Red, false, 0.0f, 0, 1.0f);   // 12
	Queue.DrawDebugBox(FVector(10.0, 0.0, 0.0), FVector(1.0), FQuat::Identity, FColor::Red, false, 0.0f, 0, 1.0f);  // 24
	Queue.DrawDebugBox(FVector(20.0, 0.0, 0.0), FVector(1.0), FQuat::Identity, FColor::Red, false, 0.0f, 0, 1.0f);  // 36 > 30: rejected
	Queue.DrawDebugLine(FVector(5000.0, 0.0, 0.0), FVector(5001.0, 0.0, 0.0), FColor::Red);                        // outside the region
	Queue.DrawDebugSphere(FVector(0.0, 0.0, 0.0), 10.0, 8, FColor::Red);                                           // 64: rejected
	CHECK(Queue.Num() == 2);
	CHECK(Queue.GetStats().NumAccepted == 2);
	CHECK(Queue.GetStats().NumRejectedBudget == 2);
	CHECK(Queue.GetStats().NumRejectedRegion == 1);
	CHECK(Queue.GetStats().RequestedCost == 12 + 12 + 12 + 64);   // the cost accumulates even for rejected commands (AddToCost)
	std::vector<FLatentDrawCommand> Commands;
	Queue.ExtractAllElements(Commands);
	CHECK(Commands.size() == 2u);
	CHECK(Queue.Num() == 0);
	CHECK(Queue.GetStats().RequestedCost == 0);
	// Budget 0 and radius 0 are infinite.
	Queue.SetMaxCost(0);
	Queue.SetRegionOfInterest(FVector::ZeroVector, 0.0f);
	for (int32 Index = 0; Index < 5000; ++Index)
	{
		Queue.DrawDebugBox(FVector(Index * 1000.0, 0.0, 0.0), FVector(1.0), FQuat::Identity, FColor::Red, false, 0.0f, 0, 1.0f);
	}
	CHECK(Queue.Num() == 5000);
	// Bounds: inside the radius when the box comes within it (FChaosDDFrame::IsInDrawRegion(FBox)).
	Queue.SetRegionOfInterest(FVector::ZeroVector, 100.0f);
	CHECK(Queue.IsInRegionOfInterest(FBox(FVector(50.0, -10.0, -10.0), FVector(5000.0, 10.0, 10.0))));
	CHECK_FALSE(Queue.IsInRegionOfInterest(FBox(FVector(150.0, -10.0, -10.0), FVector(5000.0, 10.0, 10.0))));
	// Capture costs of FChaosDDPrimitives (5.8.3 new queue): circle / arrow 1 whatever the segments, sphere 64, capsule 16,
	// string 10 whatever its length.
	FLatentDrawCommand Circle;
	Circle.Type = EDrawType::Circle;
	Circle.Segments = 12;
	CHECK(FDebugDrawQueue::GetCommandCost(Circle) == 1);
	FLatentDrawCommand Capsule;
	Capsule.Type = EDrawType::Capsule;
	CHECK(FDebugDrawQueue::GetCommandCost(Capsule) == 16);
	FLatentDrawCommand Arrow;
	Arrow.Type = EDrawType::DirectionalArrow;
	CHECK(FDebugDrawQueue::GetCommandCost(Arrow) == 1);
	FLatentDrawCommand Text;
	Text.Type = EDrawType::String;
	Text.Text = "1000.0 kg";
	CHECK(FDebugDrawQueue::GetCommandCost(Text) == 10);
	FLatentDrawCommand Sphere;
	Sphere.Type = EDrawType::Sphere;
	Sphere.Segments = 8;
	CHECK(FDebugDrawQueue::GetCommandCost(Sphere) == 64);

	// Every command is culled by its own bounds, not its anchor point: the 200 m ground box of the M1 / M3 scenes (centre
	// 0,0,-50) stays when the camera hovers over it 50 m from that centre with the default radius of 30 m. The legacy
	// anchor-point test rejected it.
	FDebugDrawQueue Ground;
	Ground.SetMaxCost(20000);
	Ground.SetRegionOfInterest(FVector(5000.0, 0.0, 300.0), 3000.0f);
	Ground.DrawDebugBox(FVector(0.0, 0.0, -50.0), FVector(10000.0, 10000.0, 50.0), FQuat::Identity, FColor::Red, false, 0.0f, 0, 1.0f);
	CHECK(Ground.Num() == 1);
	CHECK(Ground.GetStats().NumRejectedRegion == 0);
	// UE's box bounds are FBox(-0.5 * Size, 0.5 * Size) of the half extent: half of the drawn box (DebugDrawPrimitives.cpp:89).
	FLatentDrawCommand GroundCommand;
	GroundCommand.Type = EDrawType::Box;
	GroundCommand.Center = FVector(0.0, 0.0, -50.0);
	GroundCommand.Extent = FVector(10000.0, 10000.0, 50.0);
	const FBox GroundBounds = FDebugDrawQueue::GetCommandBounds(GroundCommand);
	CHECK(GroundBounds.Min.X == doctest::Approx(-5000.0));
	CHECK(GroundBounds.Max.Z == doctest::Approx(-25.0));
	// A capsule's bounds are its axis segment (no radius): HalfHeight 50 along a pitched-90 axis = world X.
	FLatentDrawCommand Lying;
	Lying.Type = EDrawType::Capsule;
	Lying.Rotation = FRotator(90.0, 0.0, 0.0).Quaternion();
	Lying.HalfHeight = 50.0;
	Lying.Radius = 20.0;
	const FBox LyingBounds = FDebugDrawQueue::GetCommandBounds(Lying);
	CHECK(LyingBounds.Max.X == doctest::Approx(50.0));
	CHECK(LyingBounds.Max.Z == doctest::Approx(0.0).epsilon(1e-9));

	// Shapes take FChaosDDParticleShape's path: cost 1 each, culled by the particle's bounds only; a hidden shape
	// (ShouldRender false at render time in UE) still pays its capture cost.
	FDebugDrawQueue Shapes;
	Shapes.SetMaxCost(3);
	Shapes.SetRegionOfInterest(FVector::ZeroVector, 100.0f);
	const FBox Near(FVector(-10.0), FVector(10.0));
	const FBox Far(FVector(500.0), FVector(510.0));
	Shapes.DrawParticleShape(Near, EDrawType::Box, FVector::ZeroVector, FQuat::Identity, FVector(1000.0), 0.0, 0.0, FColor::Yellow, 1.0f);
	Shapes.DrawParticleShape(Far, EDrawType::Box, FVector::ZeroVector, FQuat::Identity, FVector(1.0), 0.0, 0.0, FColor::Yellow, 1.0f);   // region
	Shapes.DrawParticleShape(Near, EDrawType::Sphere, FVector::ZeroVector, FQuat::Identity, FVector::ZeroVector, 5.0, 0.0, FColor::Red, 1.0f, false);
	Shapes.DrawParticleShape(Near, EDrawType::Capsule, FVector::ZeroVector, FQuat::Identity, FVector::ZeroVector, 5.0, 15.0, FColor::Red, 1.0f);
	Shapes.DrawParticleShape(Near, EDrawType::Box, FVector::ZeroVector, FQuat::Identity, FVector(1.0), 0.0, 0.0, FColor::Red, 1.0f);   // 4 > 3
	CHECK(Shapes.Num() == 2);                              // the first box and the capsule
	CHECK(Shapes.GetStats().RequestedCost == 4);
	CHECK(Shapes.GetStats().NumRejectedRegion == 1);
	CHECK(Shapes.GetStats().NumRejectedBudget == 1);
	CHECK(Shapes.GetStats().WasBudgetExceeded());

	// The legacy-only DrawDebugCoordinateSystem: one acceptance of cost 3 at its position, three latent lines.
	FDebugDrawQueue Legacy;
	Legacy.SetMaxCost(0);
	Legacy.SetRegionOfInterest(FVector::ZeroVector, 0.0f);
	Legacy.DrawDebugCoordinateSystem(FVector(1.0, 2.0, 3.0), FRotator(0.0, 90.0, 0.0), 10.0);
	CHECK(Legacy.Num() == 3);
	CHECK(Legacy.GetStats().RequestedCost == 3);
	std::vector<FLatentDrawCommand> LegacyCommands;
	Legacy.ExtractAllElements(LegacyCommands);
	CHECK(std::all_of(LegacyCommands.begin(), LegacyCommands.end(), [](const FLatentDrawCommand& C) { return C.bLatent; }));
	// The console variables carry the design defaults.
	DebugDraw::ResetConsoleVariables();
	FDebugDrawSettings Settings;
	FDebugDrawFlags Flags;
	DebugDraw::ReadConsoleVariables(Settings, Flags);
	CHECK(Flags.MaxLines == 20000);
	CHECK(Flags.Radius == doctest::Approx(3000.0f));
	CHECK(Settings.LineThickness == doctest::Approx(1.0f));
	CHECK(Settings.PointSize == doctest::Approx(5.0f));
	CHECK(Settings.ContactLen == doctest::Approx(30.0f));
	CHECK(Settings.ContactWidth == doctest::Approx(6.0f));
	CHECK(Settings.BodyAxisLen == doctest::Approx(30.0f));
	CHECK(Settings.ArrowSize == doctest::Approx(10.0f));
	CHECK(Settings.DrawPriority == 10);
	CHECK(IConsoleManager::Get().FindConsoleVariable("p.Chaos.Solver.DebugDrawIslands") != nullptr);
	CHECK(IConsoleManager::Get().FindConsoleVariable("p.Chaos.DebugDraw.SeeThrough") != nullptr);
}

// FChaosDDRenderer, the render stage of 5.8.3 (ChaosDebugDrawSubsystem.cpp:50-246): its own region test (squared distance <= W^2)
// and the same p.Chaos.DebugDraw.MaxLines as a render budget, where an accepted command is charged twice (TryAddToCost).
// M3.BoxPile 1k at the default budget: the 1001 box shapes cost 1001 at capture but 24 each at render, so UE renders 833 of
// them and reports "Debug Draw Render Budget Exceeded".
TEST_CASE("PhysicsTest.DebugDraw.RenderStageBudgetAndRegion")
{
	FRecordingRenderer Target;
	std::vector<FLatentDrawCommand> Commands;
	{
		FDebugDrawQueue Queue;
		Queue.SetMaxCost(20000);
		Queue.SetRegionOfInterest(FVector::ZeroVector, 0.0f);
		for (int32 Index = 0; Index < 1001; ++Index)
		{
			const FVector Center(Index * 150.0, 0.0, 50.0);
			Queue.DrawParticleShape(FBox(Center - FVector(50.0), Center + FVector(50.0)), EDrawType::Box, Center, FQuat::Identity, FVector(50.0), 0.0, 0.0,
				FColor::Yellow, 1.0f);
		}
		CHECK(Queue.GetStats().RequestedCost == 1001);
		CHECK_FALSE(Queue.GetStats().WasBudgetExceeded());
		Queue.ExtractAllElements(Commands);
	}
	REQUIRE(Commands.size() == 1001u);
	FDebugDrawRenderStage Stage(Target, FSphere(FVector::ZeroVector, 0.0), 20000);
	VisitCommands(Commands, Stage);
	CHECK(Stage.GetStats().NumRendered == 833);
	CHECK(Stage.GetStats().NumRejectedBudget == 168);
	CHECK(Stage.GetStats().RenderCost == 833 * 24 + 168 * 12);
	CHECK(Stage.GetStats().WasRenderBudgetExceeded());
	CHECK(Target.Calls.size() == 833u);
	MESSAGE("BoxPile-like 1001 boxes at MaxLines 20000: capture cost 1001, render " << Stage.GetStats().NumRendered << " drawn, cost "
		<< Stage.GetStats().RenderCost);
	// Budget 0 is infinite at render too.
	FRecordingRenderer All;
	FDebugDrawRenderStage Unlimited(All, FSphere(FVector::ZeroVector, 0.0), 0);
	VisitCommands(Commands, Unlimited);
	CHECK(All.Calls.size() == 1001u);
	CHECK_FALSE(Unlimited.GetStats().WasRenderBudgetExceeded());

	// Region: "<= W^2" at render ("< W^2" at capture); UE's box bounds are half the drawn box. A box of half extent 10 at
	// x = 105 has bounds [100, 110] (half of 10 -> 5 around 105): on the edge of a 100 cm region.
	FRecordingRenderer Near;
	FDebugDrawRenderStage Region(Near, FSphere(FVector::ZeroVector, 100.0), 0);
	Region.RenderBox(FVector(105.0, 0.0, 0.0), FQuat::Identity, FVector(10.0), FColor::Red, 1.0f, 0.0f);   // distance 100: drawn
	Region.RenderBox(FVector(150.0, 0.0, 0.0), FQuat::Identity, FVector(10.0), FColor::Red, 1.0f, 0.0f);   // distance 145: culled
	Region.RenderLine(FVector(-500.0, 0.0, 0.0), FVector(500.0, 0.0, 0.0), FColor::Red, 1.0f, 0.0f);      // bounds contain the centre
	CHECK(Near.Calls.size() == 2u);
	CHECK(Region.GetStats().NumRejectedRegion == 1);
	FDebugDrawQueue Capture;
	Capture.SetRegionOfInterest(FVector::ZeroVector, 100.0f);
	CHECK_FALSE(Capture.IsInRegionOfInterest(FBox(FVector(100.0, -5.0, -5.0), FVector(110.0, 5.0, 5.0))));   // capture: strict
	CHECK(Region.IsInDrawRegion(FBox(FVector(100.0, -5.0, -5.0), FVector(110.0, 5.0, 5.0))));               // render: <=
	// Harness: W = 0 is everywhere at render as well (UE 5.8.3's render stage has no W > 0 guard).
	CHECK(Unlimited.IsInDrawRegion(FBox(FVector(1.0e6), FVector(1.0e6 + 1.0))));
	// Render costs (FChaosDDRenderer): box 12, circle 8, sphere 16, arrow 3, string 10, capsule 16.
	FLatentDrawCommand Probe;
	Probe.Type = EDrawType::Circle;
	CHECK(FDebugDrawRenderStage::GetRenderCost(Probe) == 8);
	Probe.Type = EDrawType::Sphere;
	CHECK(FDebugDrawRenderStage::GetRenderCost(Probe) == 16);
	Probe.Type = EDrawType::DirectionalArrow;
	CHECK(FDebugDrawRenderStage::GetRenderCost(Probe) == 3);
	Probe.Type = EDrawType::String;
	CHECK(FDebugDrawRenderStage::GetRenderCost(Probe) == 10);

	// Latent (legacy-API) commands are rendered first and outside the budget (RenderFrame: VisitLatentCommands first).
	std::vector<FLatentDrawCommand> Mixed;
	{
		FDebugDrawQueue Queue;
		Queue.DrawDebugBox(FVector::ZeroVector, FVector(10.0), FQuat::Identity, FColor::Yellow, false, 0.0f, 0, 1.0f);
		Queue.DrawDebugCoordinateSystem(FVector::ZeroVector, FRotator(), 10.0);
		Queue.ExtractAllElements(Mixed);
	}
	REQUIRE(Mixed.size() == 4u);
	FRecordingRenderer Ordered;
	FDebugDrawRenderStage Tight(Ordered, FSphere(FVector::ZeroVector, 0.0), 12);   // room for the box only (12 <= 12, then 24)
	VisitCommands(Mixed, Tight);
	REQUIRE(Ordered.Calls.size() == 4u);
	CHECK(Ordered.Calls[0].Name == "Line");
	CHECK(Ordered.Calls[0].Color == FColor::Red);
	CHECK(Ordered.Calls[3].Name == "Box");
	CHECK(Tight.GetStats().NumLatent == 3);
	CHECK(Tight.GetStats().RenderCost == 24);
}

// UE's IsDynamic() is Dynamic or Sleeping: a sleeping body's colliding shapes are Yellow (DrawCollidingShapesImpl), its mass
// text is Yellow; a kinematic body's mass text is Cyan and drawn before the dynamic ones (PostTickDebugDraw draws the
// kinematic view first); an isolated sleeping body is not drawn in the sleep-state view (it is not in UE's active view).
TEST_CASE("PhysicsTest.DebugDraw.SleepingAndKinematicColors")
{
	const FTestScene Test;
	FPhysicsDebugSnapshot Snapshot = MakeSyntheticSnapshot(Test);
	Snapshot.Particles[1].ObjectState = static_cast<uint8>(EObjectStateType::Sleeping);   // the box of contacts 0 and 1
	std::vector<int32> ParticleOfBody;
	DebugDraw::BuildParticleLookup(Snapshot, ParticleOfBody);
	const FDebugDrawSettings Settings;
	FDebugDrawFlags Flags;
	Flags.Radius = 0.0f;
	const DebugDraw::FDrawContext Context{ *Test.Scene, Snapshot, Settings, Flags, ParticleOfBody };
	const std::vector<FLatentDrawCommand> Colliding = Run([&](FDebugDrawQueue& Queue) { DebugDraw::DrawCollidingShapes(Queue, Context); });
	// Contacts: box(1)-ground(0), capsule(2)-box(1): the sleeping box, the static ground, the dynamic capsule.
	REQUIRE(Colliding.size() == 3u);
	CHECK(Colliding[0].Color == FColor::Yellow);   // body 1, sleeping
	CHECK(Colliding[1].Color == FColor::Red);      // body 0, static
	CHECK(Colliding[2].Color == FColor::Yellow);   // body 2, dynamic
	const std::vector<FLatentDrawCommand> Mass = Run([&](FDebugDrawQueue& Queue) { DebugDraw::DrawParticleMass(Queue, Context); });
	REQUIRE(Mass.size() == 4u);                     // kinematic, then box, capsule, sphere
	CHECK(Mass[0].Color == FColor::Cyan);
	CHECK(Mass[0].TextLocation.Y == doctest::Approx(300.0));   // the kinematic box at y 300
	CHECK(Mass[1].Color == FColor::Yellow);         // sleeping box
	CHECK(Mass[3].Color == FColor::Yellow);
	MESSAGE("mass texts: " << Mass[0].Text << " (kinematic, cyan), " << Mass[1].Text << " (sleeping, yellow)");
	// Sleep state: the sphere (body 3) has no contact. Awake it is an isolated particle and drawn; asleep it has left UE's
	// active particle view (DeactivateParticle), which the isolated loop walks, so nothing is drawn for it.
	const auto CountSpheres = [&](const FPhysicsDebugSnapshot& Source)
	{
		FPhysicsDebugSnapshot Built = Source;
		FIslandBuilder::Build(Built, Test.Scene->GetConstraints());
		REQUIRE(Built.HarnessIslandIndex[3] == INDEX_NONE);
		const DebugDraw::FDrawContext BuiltContext{ *Test.Scene, Built, Settings, Flags, ParticleOfBody };
		const std::vector<FLatentDrawCommand> Sleep = Run([&](FDebugDrawQueue& Queue) { DebugDraw::DrawSleepState(Queue, BuiltContext); });
		return std::count_if(Sleep.begin(), Sleep.end(), [](const FLatentDrawCommand& C) { return C.Type == EDrawType::Sphere; });
	};
	CHECK(CountSpheres(Snapshot) == 1);
	FPhysicsDebugSnapshot Isolated = Snapshot;
	Isolated.Particles[3].ObjectState = static_cast<uint8>(EObjectStateType::Sleeping);
	CHECK(CountSpheres(Isolated) == 0);
}
// Null has no backend snapshot: the host state stands in (bodies, states, COM, bounds; no contacts).
TEST_CASE("PhysicsTest.DebugDraw.HostSnapshotForNull")
{
	const FTestScene Test;
	FPhysicsDebugSnapshot Direct;
	CHECK_FALSE(Test.Scene->CaptureDebugSnapshot(Direct));   // PT_E_UNSUPPORTED
	FPhysicsDebugSnapshot Snapshot;
	DebugDraw::CaptureSnapshot(*Test.Scene, PT_SNAP_PARTICLES | PT_SNAP_CONTACTS, Snapshot);
	CHECK(Snapshot.bFromHostState);
	REQUIRE(Snapshot.Particles.size() == 5u);
	CHECK(Snapshot.Contacts.empty());
	CHECK(Snapshot.Particles[0].ObjectState == static_cast<uint8>(EObjectStateType::Static));
	CHECK(Snapshot.Particles[1].ObjectState == static_cast<uint8>(EObjectStateType::Dynamic));
	CHECK(Snapshot.Particles[4].ObjectState == static_cast<uint8>(EObjectStateType::Kinematic));
	CHECK(Snapshot.Particles[0].BoundsMin.X == doctest::Approx(-1000.0));
	CHECK(Snapshot.Particles[0].BoundsMax.Z == doctest::Approx(0.0));
	CHECK(Snapshot.Particles[3].BoundsMax.Z == doctest::Approx(50.0));   // sphere r 25 at z 25
	CHECK(Snapshot.Particles[3].CenterOfMassWorld.Translation.Z == doctest::Approx(25.0));
}

// 03 M4 "조인트 드로는 합성 스냅샷으로 검증한다" (the joints themselves are M9): actor connectors white, frame a red, frame b
// cyan, the stretch magenta (default joint features, ChaosDebugDraw.cpp:1816-1922).
TEST_CASE("PhysicsTest.DebugDraw.JointsFromSyntheticSnapshot")
{
	const FTestScene Test;
	const FPhysicsDebugSnapshot Snapshot = MakeSyntheticSnapshot(Test);
	std::vector<int32> ParticleOfBody;
	DebugDraw::BuildParticleLookup(Snapshot, ParticleOfBody);
	const FDebugDrawSettings Settings;
	FDebugDrawFlags Flags;
	Flags.Radius = 0.0f;
	const DebugDraw::FDrawContext Context{ *Test.Scene, Snapshot, Settings, Flags, ParticleOfBody };
	const std::vector<FLatentDrawCommand> Joints = Run([&](FDebugDrawQueue& Queue) { DebugDraw::DrawJointConstraints(Queue, Context); });
	REQUIRE(Joints.size() == 5u);
	CHECK(Joints[0].Color == FColor::White);
	CHECK(Joints[1].Color == FColor::White);
	CHECK(Joints[2].Color == FColor::Red);
	CHECK(Joints[3].Color == FColor::Cyan);
	CHECK(Joints[4].Color == FColor::Magenta);
	CHECK(Joints[0].Thickness == doctest::Approx(1.5f));
	CHECK(Joints[4].Thickness == doctest::Approx(3.0f));
	// a = particle[1] = Body2 (the capsule at x 200) with Frame2World (x 170); b = Body1 (the box) with Frame1World (x 50).
	CHECK(Joints[0].LineStart.X == doctest::Approx(200.0));
	CHECK(Joints[2].LineEnd.X == doctest::Approx(170.0));
	CHECK(Joints[3].LineEnd.X == doctest::Approx(50.0));
	CHECK(Joints[4].LineStart.X == doctest::Approx(170.0));
	CHECK(Joints[4].LineEnd.X == doctest::Approx(50.0));
}

namespace
{
	std::vector<std::string> JoltLikeBackends()
	{
		std::vector<std::string> Names;
		for (const std::string& Name : FPhysicsBackendRegistry::Get().GetAvailableNames())
		{
			if (Name == "Jolt")
			{
				Names.push_back(Name);
			}
		}
		// A build with Jolt must list it: these cases would otherwise pass without running anything.
		if constexpr (PT_BUILD_WITH_JOLT != 0)
		{
			REQUIRE_MESSAGE(Names.size() == 1u, "PT_WITH_JOLT build, but the backend registry does not list Jolt");
		}
		return Names;
	}

	FResolvedPhysicsConfig LoadVizConfig(const std::string& Backend, const std::string& Profile)
	{
		FBenchRunSpec Spec;
		Spec.Backend = Backend;
		Spec.PhysicsProfile = Profile;
		Spec.ConfigDir = DebugDrawConfigDir();
		FResolvedPhysicsConfig Config;
		FBenchConfig Files;
		std::string Error;
		REQUIRE_MESSAGE(LoadBenchConfig(Spec, Config, Files, Error), Error);
		Config.TestSettings.RunMode = EPhysicsRunMode::Viz;   // the contact listener (viz only)
		return Config;
	}

	std::unique_ptr<FPhysScene> MakeVizScene(const char* ScenarioName, const FScenarioParams& Overrides, const std::string& Backend, const FResolvedPhysicsConfig& Config)
	{
		IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(ScenarioName);
		REQUIRE(Scenario != nullptr);
		FScenarioParams Params;
		std::string Error;
		REQUIRE(Scenario->ResolveParams(Overrides, Params, Error));
		const FScenarioCapacity Capacity = Scenario->GetCapacity(Params);
		FPhysSceneInitParams Init;
		Init.DebugName = ScenarioName;
		Init.BackendName = Backend;
		Init.Config = &Config;
		Init.MaxBodiesHint = Capacity.MaxBodies;
		Init.MaxBodyPairsHint = Capacity.MaxBodyPairs;
		Init.MaxContactConstraintsHint = Capacity.MaxContactConstraints;
		auto Scene = std::make_unique<FPhysScene>(Init);
		FRandomStream Random(Scenario->GetSeed(Params));
		const FScenarioContext Context{ Params, Random, &Config };
		Scenario->BuildScene(*Scene, Context);
		Scene->PrepareForSimulation();
		return Scene;
	}
}

// M3 review minor (1): each Jolt contact point has its own Phi = -(p1 - p2) . n. M1.SingleBoxDrop b lands on an edge: the
// points of one manifold differ in depth; with the manifold's mPenetrationDepth they would all be equal.
TEST_CASE("PhysicsTest.DebugDraw.JoltPerPointPhi")
{
	for (const std::string& Backend : JoltLikeBackends())
	{
		CAPTURE(Backend);
		const FResolvedPhysicsConfig Config = LoadVizConfig(Backend, "Normalized");
		std::unique_ptr<FPhysScene> Scene = MakeVizScene("M1.SingleBoxDrop", { { "Case", "b" } }, Backend, Config);
		double LargestSpread = 0.0;
		int32 FrameOfSpread = -1;
		for (int32 Frame = 0; Frame < 150; ++Frame)
		{
			Scene->Tick(Config.TestSettings.FixedDeltaTime);
			FPhysicsDebugSnapshot Snapshot;
			REQUIRE(Scene->CaptureDebugSnapshot(Snapshot));
			if (Snapshot.Contacts.size() < 2u)
			{
				continue;
			}
			double MinPhi = Snapshot.Contacts[0].Phi;
			double MaxPhi = Snapshot.Contacts[0].Phi;
			for (const PtContactPoint& Contact : Snapshot.Contacts)
			{
				MinPhi = std::min(MinPhi, Contact.Phi);
				MaxPhi = std::max(MaxPhi, Contact.Phi);
				// Each point's Phi is the distance between its two points along the normal (Chaos sign: separated > 0).
				const FVector Delta(Contact.PointOn0.X - Contact.PointOn1.X, Contact.PointOn0.Y - Contact.PointOn1.Y, Contact.PointOn0.Z - Contact.PointOn1.Z);
				const FVector Normal(Contact.Normal.X, Contact.Normal.Y, Contact.Normal.Z);
				CHECK((Delta | Normal) == doctest::Approx(Contact.Phi).epsilon(1e-6));
			}
			if (MaxPhi - MinPhi > LargestSpread)
			{
				LargestSpread = MaxPhi - MinPhi;
				FrameOfSpread = Frame;
			}
		}
		MESSAGE(Backend << ": largest Phi spread within one snapshot " << LargestSpread << " cm (frame " << FrameOfSpread << ")");
		CHECK(LargestSpread > 0.1);
	}
}

// Jolt reports no contacts for sleeping bodies; the snapshot keeps a sleeping island's contacts (PT_CONTACT_SLEEPING, with the
// flags of their last stepped state), so the resting box of M1.SingleBoxDrop a under UEParity (sleep on) is one sleeping
// island: drawn Black (03 M4 "잠든 섬은 Black"), and its resting contacts stay active.
TEST_CASE("PhysicsTest.DebugDraw.JoltSleepingIslandKeepsContacts")
{
	for (const std::string& Backend : JoltLikeBackends())
	{
		CAPTURE(Backend);
		const FResolvedPhysicsConfig Config = LoadVizConfig(Backend, "UEParity");
		std::unique_ptr<FPhysScene> Scene = MakeVizScene("M1.SingleBoxDrop", {}, Backend, Config);
		for (int32 Frame = 0; Frame < 200; ++Frame)
		{
			Scene->Tick(Config.TestSettings.FixedDeltaTime);
		}
		const FBodyInstance& Box = *Scene->GetBodies()[1];
		REQUIRE(Box.State.ObjectState == EObjectStateType::Sleeping);
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot));
		REQUIRE_FALSE(Snapshot.Contacts.empty());
		for (const PtContactPoint& Contact : Snapshot.Contacts)
		{
			CHECK((Contact.Flags & PT_CONTACT_SLEEPING) != 0u);
			// The last stepped state is kept: a resting point stays active, as Chaos keeps a sleeping constraint's results.
			CHECK(((Contact.Flags & PT_CONTACT_ACTIVE_APPROX) != 0u) == (Contact.Phi <= 0.0));
			CHECK(((Contact.Flags & PT_CONTACT_SPECULATIVE) != 0u) == (Contact.Phi > 0.0));
			CHECK(Contact.Normal.Z > 0.999);
			CHECK(Contact.Phi <= 0.5);
		}
		FIslandBuilder::Build(Snapshot, Scene->GetConstraints());
		std::vector<FHarnessIsland> Islands;
		FIslandBuilder::GatherIslands(Snapshot, Islands);
		REQUIRE(Islands.size() == 1u);
		CHECK_FALSE(Islands[0].bAwake);
		CHECK(DebugDraw::GetIslandColor(0, Islands[0].bAwake) == FColor::Black);
		MESSAGE(Backend << ": sleeping box keeps " << Snapshot.Contacts.size() << " contact points; island " << Islands[0].Id << " asleep -> Black");
		// Waking the box drops the kept contacts on the next step (the listener reports fresh ones).
		Scene->GetBodies()[1]->WakeInstance();
		Scene->Tick(Config.TestSettings.FixedDeltaTime);
		FPhysicsDebugSnapshot Awake;
		REQUIRE(Scene->CaptureDebugSnapshot(Awake));
		for (const PtContactPoint& Contact : Awake.Contacts)
		{
			CHECK((Contact.Flags & PT_CONTACT_SLEEPING) == 0u);
		}
	}
}

// The kept contacts follow the body commands (M4 fix round 2, FlushKeptContactCommands): a box put to sleep between steps
// (PT_CMD_PUT_TO_SLEEP) keeps the contacts of the last step, as if it had fallen asleep in it; setting its pose while it
// sleeps (SET_BODY_STATE, DontActivate: it stays asleep) drops them, since their points would be stale; after a wake and a
// second PUT_TO_SLEEP they are kept again.
TEST_CASE("PhysicsTest.DebugDraw.JoltKeptContactsFollowCommands")
{
	for (const std::string& Backend : JoltLikeBackends())
	{
		CAPTURE(Backend);
		const FResolvedPhysicsConfig Config = LoadVizConfig(Backend, "UEParity");
		std::unique_ptr<FPhysScene> Scene = MakeVizScene("M1.SingleBoxDrop", {}, Backend, Config);
		FBodyInstance& Box = *Scene->GetBodies()[1];
		const float Dt = Config.TestSettings.FixedDeltaTime;
		const auto CountContacts = [&Scene](uint32& OutSleeping)
		{
			FPhysicsDebugSnapshot Snapshot;
			REQUIRE(Scene->CaptureDebugSnapshot(Snapshot));
			OutSleeping = 0;
			for (const PtContactPoint& Contact : Snapshot.Contacts)
			{
				OutSleeping += (Contact.Flags & PT_CONTACT_SLEEPING) != 0u ? 1u : 0u;
			}
			return static_cast<uint32>(Snapshot.Contacts.size());
		};
		// The first frame the awake box touches the ground (it would fall asleep on its own about 0.5 s later).
		uint32 Touching = 0;
		uint32 Sleeping = 0;
		for (int32 Frame = 0; Frame < 200 && Touching == 0; ++Frame)
		{
			Scene->Tick(Dt);
			Touching = CountContacts(Sleeping);
		}
		REQUIRE(Touching > 0u);
		REQUIRE(Box.State.ObjectState != EObjectStateType::Sleeping);
		CHECK(Sleeping == 0u);
		// PUT_TO_SLEEP between steps: Jolt reports nothing for the sleeping box; its last-step contacts are kept.
		Box.PutInstanceToSleep();
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			Scene->Tick(Dt);
			CHECK(Box.State.ObjectState == EObjectStateType::Sleeping);
			CHECK(CountContacts(Sleeping) == Touching);
			CHECK(Sleeping == Touching);
		}
		// SET_BODY_STATE leaves it asleep (DontActivate) and drops the kept pairs.
		FPhysicsInterface::SetBodyState_AssumesLocked(Box.ActorHandle, Box.State.BodyTM, FVector::ZeroVector, FVector::ZeroVector);
		Scene->Tick(Dt);
		CHECK(Box.State.ObjectState == EObjectStateType::Sleeping);
		CHECK(CountContacts(Sleeping) == 0u);
		// Awake again, the listener reports the pair; a second PUT_TO_SLEEP keeps it once more (no duplicates).
		Box.WakeInstance();
		Scene->Tick(Dt);
		const uint32 Rewoken = CountContacts(Sleeping);
		CHECK(Rewoken > 0u);
		CHECK(Sleeping == 0u);
		Box.PutInstanceToSleep();
		Scene->Tick(Dt);
		CHECK(CountContacts(Sleeping) == Rewoken);
		CHECK(Sleeping == Rewoken);
		MESSAGE(Backend << ": put to sleep keeps " << Touching << " contact points; SET_BODY_STATE drops them; re-slept keeps " << Rewoken);
	}
}

// M3 follow-up for M4 CollidingShapes: Jolt contacts carry the harness element index of the touching sub shape. A body with
// a sphere (element 0, on top) and a box (element 1, at the bottom) rests on the ground on its box: Shape0 = 1. The body is
// a StaticCompoundShape inside an OffsetCenterOfMassShape, so the decorator path is exercised too.
TEST_CASE("PhysicsTest.DebugDraw.JoltContactElementIndex")
{
	for (const std::string& Backend : JoltLikeBackends())
	{
		CAPTURE(Backend);
		const FResolvedPhysicsConfig Config = LoadVizConfig(Backend, "Normalized");
		FPhysSceneInitParams Init;
		Init.DebugName = "ElementIndex";
		Init.BackendName = Backend;
		Init.Config = &Config;
		FPhysScene Scene(Init);
		FBodySetup GroundSetup;
		GroundSetup.AggGeom.BoxElems.emplace_back(2000.0, 2000.0, 100.0);
		FBodySetup TwoElements;
		FKSphereElem Sphere(25.0);
		Sphere.Center = FVector(0.0, 0.0, 150.0);
		TwoElements.AggGeom.SphereElems.push_back(Sphere);
		TwoElements.AggGeom.BoxElems.emplace_back(60.0);
		const FBodySetup* GroundPtr = Scene.CreateBodySetup(std::move(GroundSetup));
		const FBodySetup* TwoPtr = Scene.CreateBodySetup(std::move(TwoElements));
		FBodyInstance Static;
		Static.bStatic = true;
		Scene.SpawnBody(Static, GroundPtr, FTransform(FVector(0.0, 0.0, -50.0)));
		FBodyInstance Dynamic;
		Dynamic.bSimulatePhysics = true;
		FBodyInstance* Body = Scene.SpawnBody(Dynamic, TwoPtr, FTransform(FVector(0.0, 0.0, 60.0)));
		Scene.FlushDeferredActors();
		Scene.PrepareForSimulation();
		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			Scene.Tick(Config.TestSettings.FixedDeltaTime);
		}
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene.CaptureDebugSnapshot(Snapshot));
		REQUIRE_FALSE(Snapshot.Contacts.empty());
		for (const PtContactPoint& Contact : Snapshot.Contacts)
		{
			CHECK(Contact.Body0 == Body->ActorHandle.Index);   // the dynamic body is Body0 (more mobile)
			CHECK(Contact.Shape0 == 1u);                      // its box element
			CHECK(Contact.Shape1 == 0u);                      // the ground's only element
		}
		MESSAGE(Backend << ": " << Snapshot.Contacts.size() << " contact points on element 1 (the box) of the two-element body");
	}
}

// The magenta inertia box of DrawParticleTransforms is the harness inertia (D5) in the harness principal frame, so a
// non-cubic body is drawn with its own axes on every engine. Jolt orders the principal moments itself (its snapshot's COM
// rotation carries that order, docs/Abi.md 5.5); orienting the harness box by it would permute the box's axes.
TEST_CASE("PhysicsTest.DebugDraw.InertiaBoxUsesHarnessPrincipalFrame")
{
	for (const std::string& Backend : JoltLikeBackends())
	{
		CAPTURE(Backend);
		const FResolvedPhysicsConfig Config = LoadVizConfig(Backend, "Normalized");
		FPhysSceneInitParams Init;
		Init.DebugName = "InertiaBox";
		Init.BackendName = Backend;
		Init.Config = &Config;
		FPhysScene Scene(Init);
		FBodySetup PlateSetup;
		PlateSetup.AggGeom.BoxElems.emplace_back(200.0, 50.0, 20.0);   // full extents: a 200 x 50 x 20 cm plate
		const FBodySetup* PlatePtr = Scene.CreateBodySetup(std::move(PlateSetup));
		FBodyInstance Dynamic;
		Dynamic.bSimulatePhysics = true;
		Dynamic.bEnableGravity = false;
		FBodyInstance* Plate = Scene.SpawnBody(Dynamic, PlatePtr, FTransform(FRotator(0.0, 30.0, 0.0), FVector(0.0, 0.0, 100.0)));
		Scene.FlushDeferredActors();
		Scene.PrepareForSimulation();
		Scene.Tick(Config.TestSettings.FixedDeltaTime);
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene.CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES));
		std::vector<int32> ParticleOfBody;
		DebugDraw::BuildParticleLookup(Snapshot, ParticleOfBody);
		const FDebugDrawSettings Settings;
		FDebugDrawFlags Flags;
		Flags.Radius = 0.0f;
		const DebugDraw::FDrawContext Context{ Scene, Snapshot, Settings, Flags, ParticleOfBody };
		const std::vector<FLatentDrawCommand> Transforms = Run([&](FDebugDrawQueue& Queue) { DebugDraw::DrawParticleTransforms(Queue, Context); });
		const auto Box = std::find_if(Transforms.begin(), Transforms.end(), [](const FLatentDrawCommand& C) { return C.Type == EDrawType::Box && C.Color == FColor::Magenta; });
		REQUIRE(Box != Transforms.end());
		// The box's half extents along the plate's own axes (yaw 30): 100 x 25 x 10, whatever order the engine keeps its moments in.
		const FQuat PlateRotation = FRotator(0.0, 30.0, 0.0).Quaternion();
		const FVector PlateAxes[3] = { PlateRotation.GetAxisX(), PlateRotation.GetAxisY(), PlateRotation.GetAxisZ() };
		const FVector BoxAxes[3] = { Box->Rotation.GetAxisX(), Box->Rotation.GetAxisY(), Box->Rotation.GetAxisZ() };
		const double Expected[3] = { 100.0, 25.0, 10.0 };
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			double HalfAlongPlateAxis = 0.0;
			for (int32 BoxAxis = 0; BoxAxis < 3; ++BoxAxis)
			{
				HalfAlongPlateAxis += std::abs(BoxAxes[BoxAxis] | PlateAxes[Axis]) * Box->Extent[BoxAxis];
			}
			CAPTURE(Axis);
			CHECK(HalfAlongPlateAxis == doctest::Approx(Expected[Axis]).epsilon(1e-3));
		}
		const PtParticleSnapshot& Particle = Snapshot.Particles[static_cast<size_t>(ParticleOfBody[Plate->ActorHandle.Index])];
		const FQuat EngineCom(Particle.CenterOfMassWorld.Rotation.X, Particle.CenterOfMassWorld.Rotation.Y, Particle.CenterOfMassWorld.Rotation.Z,
			Particle.CenterOfMassWorld.Rotation.W);
		MESSAGE(Backend << ": engine COM X axis . plate X axis = " << (EngineCom.GetAxisX() | PlateAxes[0]) << " (a value away from +-1 means the engine "
			<< "reorders the principal axes); harness box half extents " << Box->Extent.X << " x " << Box->Extent.Y << " x " << Box->Extent.Z);
	}
}