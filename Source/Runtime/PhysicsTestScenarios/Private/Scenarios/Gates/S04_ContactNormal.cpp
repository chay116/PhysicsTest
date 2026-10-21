// S04_ContactNormal.cpp - S04.ContactNormal (docs/Scenes.md 2.8, G3; a viz run: the contact listener exists). A 100 cm
// cube, a radius 50 cm sphere and a lying capsule (radius 25, length 100), each starting 1 cm into the ground, 100 kg,
// friction 0.5, restitution 0, damping 0, 60 frames. G3: the step-1 contacts of each body with the ground are in the
// Chaos convention after the backend's conversion (Jolt flips normal and depth, PtContactConvention.h): normal . (+Z) >
// 0.999 and Phi < 0, and PointOn1 lies on the ground, PointOn0 on the body (docs/Abi.md 7.1, M6 contract (c)).
#include "GateScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FS04ContactNormal final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return GateScenes::S04::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Gate; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "box, sphere and lying capsule starting 1 cm into the ground: step-1 contacts point along +Z with Phi < 0 (G3, viz)";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return GateScenes::S04::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, static_cast<uint32>(GateScenes::S04::Frames - 1) } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 4, 64, 64 }; }
			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace GateScenes::S04;
				const FPhysicalMaterial* Material = ScenarioHelpers::MakeMaterial(Scene, Context, "ContactNormal", 0.5f, 0.0f);
				ScenarioHelpers::SpawnGround(Scene, Context, Material);
				const FBodySetup* BoxSetup = ScenarioHelpers::MakeCubeSetup(Scene, "Cube100", BoxEdge, Material);
				FBodySetup Sphere;
				Sphere.BodySetupName = "Sphere50";
				Sphere.AggGeom.SphereElems.emplace_back(SphereRadius);
				Sphere.PhysMaterial = Material;
				const FBodySetup* SphereSetup = Scene.CreateBodySetup(std::move(Sphere));
				FBodySetup Capsule;
				Capsule.BodySetupName = "Capsule25x100";
				Capsule.AggGeom.SphylElems.emplace_back(CapsuleRadius, CapsuleLength);
				Capsule.PhysMaterial = Material;
				const FBodySetup* CapsuleSetup = Scene.CreateBodySetup(std::move(Capsule));
				Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, "Box", MassKg, true), BoxSetup,
					FTransform(FVector(-300.0, 0.0, 0.5 * BoxEdge - Penetration)));
				Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, "Sphere", MassKg, true), SphereSetup,
					FTransform(FVector(0.0, 0.0, SphereRadius - Penetration)));
				Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, "Capsule", MassKg, true), CapsuleSetup,
					FTransform(FRotator(0.0, 0.0, 90.0), FVector(300.0, 0.0, CapsuleRadius - Penetration)));
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FS04ContactNormal, S04ContactNormal)
