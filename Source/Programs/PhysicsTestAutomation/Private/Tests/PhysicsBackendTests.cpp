// PhysicsBackendTests.cpp - the host side of the ABI (03_WorkPlan M2 task 4, 02_Architecture 7.1 / 7.4, A.8): the
// generated registry, FBodyInstance -> Pt* marshalling (box half extents in one place, capsule half length, body scale
// vs BuildScale3D, channel validation, host-resolved combine modes), and the effective-settings parser.
#include "CoreMinimal.h"

#include "Physics/EffectiveSettings.h"
#include "Physics/PhysicsBackend.h"
#include "PhysScene.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace PhysicsTest;

namespace
{
	FPhysScene MakeScene()
	{
		FPhysSceneInitParams Params;
		Params.DebugName = "BackendTests";
		return FPhysScene(Params);
	}
}

TEST_CASE("PhysicsTest.Backend.RegistryListsTheGeneratedBackends")
{
	const std::vector<std::string> Names = FPhysicsBackendRegistry::Get().GetAvailableNames();
	CHECK(std::find(Names.begin(), Names.end(), "Null") != Names.end());
	CHECK(std::find(Names.begin(), Names.end(), "NullDll") != Names.end());   // PtBackend_Null.dll next to this exe
	bool bNullIsStatic = false;
	for (const FBackendRegistryEntry& Entry : GetBackendRegistryEntries())
	{
		bNullIsStatic = bNullIsStatic || (std::string(Entry.Name) == "Null" && Entry.StaticEntry != nullptr);
	}
	CHECK(bNullIsStatic);
	std::string Error;
	FPhysicsBackendModule* Null = FPhysicsBackendRegistry::Get().FindOrLoadModule("Null", Error);
	REQUIRE(Null != nullptr);
	CHECK(Null == FPhysicsBackendRegistry::Get().FindOrLoadModule("Null", Error));   // loaded once
	const FBackendInfo& Info = Null->GetInfo();
	CHECK(Info.Name == "Null");
	CHECK(Info.Has(PT_CAP_NATIVE_FRAME_MODEL));
	CHECK(Info.Has(PT_CAP_KINEMATIC | PT_CAP_ACTIVE_LIST | PT_CAP_STATE_WRITE | PT_CAP_NATIVE_HASH));
	CHECK_FALSE(Info.Has(PT_CAP_COLLISION));
	CHECK(Info.ProvisionalRevision == PT_ABI_PROVISIONAL_REVISION);
	CHECK(Info.RequiredEffectiveKeys.empty());
	CHECK(Info.BuildFlagsJson.find("\"backend\":\"Null\"") != std::string::npos);
	CHECK(Info.BuildFlagsJson.find("\"fp\":\"fast\"") != std::string::npos);
	FPhysicsBackendModule* Dll = FPhysicsBackendRegistry::Get().FindOrLoadModule("NullDll", Error);
	REQUIRE_MESSAGE(Dll != nullptr, Error);
	CHECK(Dll->IsDll());
	CHECK(Dll->GetInfo().Name == "Null");
	CHECK(&Dll->GetApi() != &Null->GetApi());
}

TEST_CASE("PhysicsTest.Backend.MarshallingOfShapesAndBodies")
{
	FPhysScene Scene = MakeScene();
	FPhysicalMaterial Ice;
	Ice.Friction = 0.05f;
	Ice.StaticFriction = 0.1f;
	Ice.Restitution = 0.2f;
	Ice.bOverrideFrictionCombineMode = true;
	Ice.FrictionCombineMode = EFrictionCombineMode::Min;
	const FPhysicalMaterial* IcePtr = Scene.CreatePhysicalMaterial(Ice);

	FBodySetup Setup;
	FKBoxElem Box(100.0, 50.0, 20.0);   // full extents
	Box.Center = FVector(1.0, 2.0, 3.0);
	Box.Rotation = FRotator(0.0, 90.0, 0.0);
	Setup.AggGeom.BoxElems.push_back(Box);
	FKSphylElem Capsule(25.0, 100.0);   // segment length 100 -> half length 50
	Setup.AggGeom.SphylElems.push_back(Capsule);
	FKSphereElem Sphere(10.0);
	Sphere.Center = FVector(0.0, 0.0, 40.0);
	Setup.AggGeom.SphereElems.push_back(Sphere);
	Setup.BuildScale3D = FVector(3.0);   // metadata only: never applied (02 5.2 #25)
	const FBodySetup* SetupPtr = Scene.CreateBodySetup(std::move(Setup));

	FBodyInstance Template;
	Template.bSimulatePhysics = true;
	Template.PhysMaterialOverride = IcePtr;
	Template.CollisionFilter.ObjectType = 5;
	Template.CollisionFilter.BlockMask = 0x0000FF00u;
	Template.bStartAwake = false;
	Template.bGyroscopicTorqueEnabled = true;
	FBodyInstance* Body = Scene.SpawnBody(Template, SetupPtr, FTransform(FQuat::Identity, FVector(0.0, 0.0, 500.0), FVector(2.0)));

	FMarshalledActorBatch Batch;
	std::string Error;
	FBodyInstance* Bodies[] = { Body };
	REQUIRE_MESSAGE(Scene.GetBackendScene().MarshalBodies(Bodies, 0, Scene.GetSettings(), Batch, Error), Error);
	REQUIRE(Batch.Actors.size() == 1u);
	REQUIRE(Batch.Shapes.size() == 3u);
	const PtActorCreateInfo& Actor = Batch.Actors[0];
	CHECK(Actor.BodyIndex == 0u);
	CHECK(Actor.Motion == PT_MOTION_DYNAMIC);
	CHECK(Actor.NumShapes == 3u);
	CHECK(Actor.ObjectType == 5u);
	CHECK(Actor.BlockMask == 0x0000FF00u);
	CHECK((Actor.Flags & PT_BODY_START_AWAKE) == 0u);
	CHECK((Actor.Flags & PT_BODY_GYROSCOPIC) != 0u);
	CHECK((Actor.Flags & PT_BODY_ENABLE_GRAVITY) != 0u);
	CHECK(Actor.ActorTransform.Translation.Z == 500.0);
	CHECK(Actor.Mass == Body->GetBodyMass());
	CHECK(Actor.MaxAngularVelocity == doctest::Approx(Body->GetMaxAngularVelocityInRadians(Scene.GetSettings())));
	// Spheres, then boxes, then capsules (FKAggregateGeom order). Body scale 2 applies once; BuildScale3D does not.
	const PtShapeElem& SphereShape = Batch.Shapes[0];
	CHECK(SphereShape.Type == PT_SHAPE_SPHERE);
	CHECK(SphereShape.Radius == 20.0);
	CHECK(SphereShape.LocalTransform.Translation.Z == 80.0);
	const PtShapeElem& BoxShape = Batch.Shapes[1];
	CHECK(BoxShape.Type == PT_SHAPE_BOX);
	CHECK(BoxShape.BoxHalfExtent.X == 100.0);   // (100 / 2) * 2
	CHECK(BoxShape.BoxHalfExtent.Y == 50.0);
	CHECK(BoxShape.BoxHalfExtent.Z == 20.0);
	CHECK(BoxShape.LocalTransform.Translation.X == 2.0);
	const FQuat Yaw = FRotator(0.0, 90.0, 0.0).Quaternion();
	CHECK(BoxShape.LocalTransform.Rotation.Z == doctest::Approx(Yaw.Z).epsilon(1e-15));
	CHECK(BoxShape.LocalTransform.Rotation.W == doctest::Approx(Yaw.W).epsilon(1e-15));
	const PtShapeElem& CapsuleShape = Batch.Shapes[2];
	CHECK(CapsuleShape.Type == PT_SHAPE_CAPSULE);
	CHECK(CapsuleShape.Radius == 50.0);
	CHECK(CapsuleShape.HalfLength == 100.0);   // UE GetFinalScaled: (100 / 2) * 2
	CHECK((BoxShape.Flags & (PT_SHAPEF_SIMULATION | PT_SHAPEF_QUERY)) == (PT_SHAPEF_SIMULATION | PT_SHAPEF_QUERY));
	// The material is new: raw friction values, the per-material combine mode resolved by the host.
	REQUIRE(Batch.NewMaterials.size() == 1u);
	CHECK(Batch.NewMaterials[0].Friction == static_cast<double>(0.05f));
	CHECK(Batch.NewMaterials[0].StaticFriction == static_cast<double>(0.1f));
	CHECK(Batch.NewMaterials[0].FrictionCombineMode == PT_COMBINE_MIN);
	CHECK(Batch.NewMaterials[0].RestitutionCombineMode == PT_COMBINE_AVERAGE);   // the project setting
	for (const PtShapeElem& Shape : Batch.Shapes)
	{
		CHECK(Shape.MaterialIndex == 0u);
	}
	Scene.FlushDeferredActors();
	CHECK(Body->ActorHandle.IsValid());
	CHECK(Body->State.ObjectState == EObjectStateType::Sleeping);
	// Mass properties read back exactly as marshalled.
	PtMassProperties Mass = {};
	REQUIRE(Scene.GetBackendScene().ReadMassProperties(0, std::span<PtMassProperties>(&Mass, 1)) == PT_OK);
	CHECK(Mass.Mass == Body->GetBodyMass());
	CHECK(Mass.InertiaDiagonal.Y == Body->GetBodyInertiaTensor().Y);

	// MaxDepenetrationVelocity, UE 5.8.3 semantics (M2 review (1), M3): -1 = the solver setting unless the body overrides
	// it (BodyInstance.cpp:3713-3722 [V]); an override of 0 stays 0 (initial overlaps are not pushed apart).
	CHECK(Actor.MaxDepenetrationVelocity == -1.0);
	FBodyInstance Override = Template;
	Override.bOverrideMaxDepenetrationVelocity = true;
	Override.MaxDepenetrationVelocity = 0.0f;
	FBodyInstance* Overriding = Scene.SpawnBody(Override, SetupPtr, FTransform(FVector(0.0, 0.0, 900.0)));
	FMarshalledActorBatch OverrideBatch;
	FBodyInstance* OverrideBodies[] = { Overriding };
	REQUIRE_MESSAGE(Scene.GetBackendScene().MarshalBodies(OverrideBodies, 1, Scene.GetSettings(), OverrideBatch, Error), Error);
	REQUIRE(OverrideBatch.Actors.size() == 1u);
	CHECK(OverrideBatch.Actors[0].MaxDepenetrationVelocity == 0.0);
	Override.MaxDepenetrationVelocity = 250.0f;
	FBodyInstance* Fast = Scene.SpawnBody(Override, SetupPtr, FTransform(FVector(0.0, 0.0, 1300.0)));
	OverrideBodies[0] = Fast;
	REQUIRE_MESSAGE(Scene.GetBackendScene().MarshalBodies(OverrideBodies, 2, Scene.GetSettings(), OverrideBatch, Error), Error);
	CHECK(OverrideBatch.Actors[0].MaxDepenetrationVelocity == 250.0);
	Scene.FlushDeferredActors();
	CHECK(Overriding->ActorHandle.IsValid());
	CHECK(Fast->ActorHandle.IsValid());
}

TEST_CASE("PhysicsTest.Backend.MarshallingRejectsInvalidBodies")
{
	// 02 5.1: an ObjectType >= PT_NUM_COLLISION_CHANNELS is a load error, also where SpawnBody's checkf is compiled out
	// (test preset); the marshalling reports instead of folding it to ObjectType mod 32.
	FPhysScene Scene = MakeScene();
	FBodySetup Setup;
	Setup.AggGeom.BoxElems.emplace_back(10.0);
	const FBodySetup* SetupPtr = Scene.CreateBodySetup(std::move(Setup));
	FBodyInstance Template;
	Template.bSimulatePhysics = true;
	FBodyInstance* Body = Scene.SpawnBody(Template, SetupPtr, FTransform::Identity);
	Body->CollisionFilter.ObjectType = 33;   // corrupted after the spawn
	FMarshalledActorBatch Batch;
	std::string Error;
	FBodyInstance* Bodies[] = { Body };
	CHECK_FALSE(Scene.GetBackendScene().MarshalBodies(Bodies, 0, Scene.GetSettings(), Batch, Error));
	CHECK(Error.find("collision channel 33") != std::string::npos);
	Body->CollisionFilter.ObjectType = 0;
	FBodySetup Empty;
	Body->BodySetup = Scene.CreateBodySetup(std::move(Empty));
	CHECK_FALSE(Scene.GetBackendScene().MarshalBodies(Bodies, 0, Scene.GetSettings(), Batch, Error));
	CHECK(Error.find("no collision element") != std::string::npos);
	Body->BodySetup = SetupPtr;
	CHECK(Scene.GetBackendScene().MarshalBodies(Bodies, 0, Scene.GetSettings(), Batch, Error));
	Scene.FlushDeferredActors();
}

TEST_CASE("PhysicsTest.Backend.EffectiveSettingsParser")
{
	std::vector<FEffectiveSetting> Settings;
	std::string Error;
	CHECK(ParseEffectiveSettings(
		"{\"key\":\"world.gravity\",\"native\":\"(0, 0, -980)\",\"native_unit\":\"cm/s^2\",\"canonical\":-980,\"unit\":\"cm/s^2\",\"source\":\"READBACK\"}\n"
		"\n"
		"{\"key\":\"solver.type\",\"native\":\"n/a\",\"native_unit\":\"\",\"canonical\":null,\"unit\":\"\",\"source\":\"COMPILE_TIME\"}\n",
		Settings, Error));
	REQUIRE(Settings.size() == 2u);
	CHECK(Settings[0].Canonical == -980.0);
	CHECK(Settings[0].bHasCanonical);
	CHECK_FALSE(Settings[1].bHasCanonical);
	CHECK_FALSE(ParseEffectiveSettings("{\"key\":\"a\"}\n", Settings, Error));
	CHECK(Error.find("native") != std::string::npos);
	CHECK_FALSE(ParseEffectiveSettings(
		"{\"key\":\"a\",\"native\":\"\",\"native_unit\":\"\",\"canonical\":1,\"unit\":\"\",\"source\":\"GUESSED\"}\n", Settings, Error));
	CHECK(Error.find("unknown source") != std::string::npos);
	CHECK_FALSE(ParseEffectiveSettings("not json\n", Settings, Error));
	const std::string Twice = "{\"key\":\"a\",\"native\":\"\",\"native_unit\":\"\",\"canonical\":1,\"unit\":\"\",\"source\":\"SET_ONLY\"}\n";
	CHECK_FALSE(ParseEffectiveSettings(Twice + Twice, Settings, Error));
	CHECK(Error.find("twice") != std::string::npos);
	// The required set: the generic keys + the backend's own.
	FBackendInfo Info;
	Info.RequiredEffectiveKeys = { "jolt.config_string" };
	const std::vector<std::string> Required = GetRequiredEffectiveKeys(Info);
	CHECK(Required.size() == GetGenericEffectiveKeys().size() + 1);
	CHECK(GetGenericEffectiveKeys().size() == 24u);
	CHECK(std::find(Required.begin(), Required.end(), "jolt.config_string") != Required.end());
	REQUIRE(ParseEffectiveSettings(Twice, Settings, Error));
	CHECK(FindMissingEffectiveKeys(Settings, Info).size() == Required.size());
}
