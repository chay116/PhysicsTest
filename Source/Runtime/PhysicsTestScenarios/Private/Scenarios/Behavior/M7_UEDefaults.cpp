// M7_UEDefaults.cpp - M7.UEDefaults (catalog extension, docs/Scenes.md 2.22, 03_WorkPlan M9, 02 10). Six bodies authored the
// way a UE level would author them: no bOverrideMass (the UE mass rule, MassScale x (rho V)^0.75 under MassRule UEBodyUtils,
// 02 5.3), the profile's default material (UE: F 0.7, SF 0, R 0.3, density 1 g/cm^3), the profile's damping, angular limit
// and sleep - 50 / 100 / 200 cm cubes, 25 / 50 cm spheres and a lying capsule (radius 25, segment 100 cm), dropped from rest
// with their bottoms 300 cm above the ground and 400 cm apart (they never touch each other). It shows what the UE defaults do
// by themselves (the ^0.75 masses, the R 0.3 bounce, damping, sleep), separately from the engines. Deterministic.
#include "BehaviorScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FM7UEDefaults final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return BehaviorScenes::M7::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Behavior; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "UE-authored bodies (no mass override, default material / damping / sleep): 50, 100, 200 cm cubes, r 25 / 50 spheres, a lying capsule; "
					"dropped 300 cm; 300 frames";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return BehaviorScenes::M7::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, BehaviorScenes::M7::Frames - 1 }, FStepWindow{ "settled", 200, BehaviorScenes::M7::Frames - 1 } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 7, 64, 64 }; }

			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace BehaviorScenes::M7;
				const FPhysicalMaterial* Material = Scene.GetDefaultPhysicalMaterial();
				ScenarioHelpers::SpawnGround(Scene, Context, Material);
				FBodyInstance Template = Context.MakeBodyTemplate();   // no bOverrideMass: the UE mass rule
				Template.bSimulatePhysics = true;
				double X = -2.5 * SpacingX;
				const auto Spawn = [&](const char* BodyName, FBodySetup&& Setup, const FRotator& Rotation, const double CentreAboveBottom)
				{
					Setup.BodySetupName = BodyName;
					Setup.PhysMaterial = Material;
					const FBodySetup* Geometry = Scene.CreateBodySetup(std::move(Setup));
					Template.BodyName = BodyName;
					Scene.SpawnBody(Template, Geometry, FTransform(Rotation, FVector(X, 0.0, DropHeight + CentreAboveBottom)));
					X += SpacingX;
				};
				for (const double Edge : { 50.0, 100.0, 200.0 })
				{
					FBodySetup Setup;
					Setup.AggGeom.BoxElems.emplace_back(Edge);
					Spawn(Edge == 50.0 ? "Cube50" : (Edge == 100.0 ? "Cube100" : "Cube200"), std::move(Setup), FRotator(0.0, 0.0, 0.0), 0.5 * Edge);
				}
				for (const double Radius : { 25.0, 50.0 })
				{
					FBodySetup Setup;
					Setup.AggGeom.SphereElems.emplace_back(Radius);
					Spawn(Radius == 25.0 ? "Sphere25" : "Sphere50", std::move(Setup), FRotator(0.0, 0.0, 0.0), Radius);
				}
				{
					FBodySetup Setup;
					Setup.AggGeom.SphylElems.emplace_back(25.0, 100.0);
					Spawn("CapsuleLying", std::move(Setup), FRotator(0.0, 0.0, 90.0), 25.0);   // Roll 90: the axis along Y (as S03)
				}
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FM7UEDefaults, M7UEDefaults)
