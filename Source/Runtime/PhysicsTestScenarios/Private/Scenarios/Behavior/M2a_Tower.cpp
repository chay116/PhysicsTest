// M2a_Tower.cpp - M2a.Tower(N) (docs/Scenes.md 2.16, 03_WorkPlan M9). N axis-aligned 100 cm cubes of 100 kg stacked exactly
// on the ground (faces in contact, no gap) on the profile's default material. A chaotic scene (statistics, and the
// same-engine Perturb replicas): the 01 probe's 10-high stack wobbled up to 25 cm under PhysX PGS 8/1 and never slept, settled
// in about 1 s with stabilization and about 2 s with TGS (03 M9: the UEParity sleep order of UE4Compat / -Stab / -TGS).
#include "BehaviorScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FM2aTower final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return BehaviorScenes::M2a::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Behavior; }
			bool IsChaotic() const override { return true; }
			const char* GetDescription() const override
			{
				return "N 100 cm cubes of 100 kg stacked exactly on the ground, default material (UE F 0.7, R 0.3), 600 frames; Perturb for sensitivity replicas";
			}
			std::vector<FScenarioParamDesc> GetParamDescs() const override
			{
				return { FScenarioParamDesc{ "N", "10", { "5", "10", "20", "30" }, "boxes in the tower" }, ScenarioHelpers::MakePerturbParamDesc() };
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return BehaviorScenes::M2a::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				// "early" = the first second (wobble), "sleep5s" = up to 5 s (03 M9: asleep within 5 s or not), "late" = 5-10 s.
				return { FStepWindow{ "headline", 1, BehaviorScenes::M2a::Frames - 1 }, FStepWindow{ "early", 1, 60 }, FStepWindow{ "sleep5s", 1, 300 },
					FStepWindow{ "late", 300, BehaviorScenes::M2a::Frames - 1 } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& Params) const override
			{
				return ScenarioHelpers::MakePileCapacity(ScenarioHelpers::ParseUnsignedParam(Params.at("N"), 10));
			}

			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace BehaviorScenes::M2a;
				const uint32 N = ScenarioHelpers::ParseUnsignedParam(Context.GetParam("N"), 10);
				const FPhysicalMaterial* Material = Scene.GetDefaultPhysicalMaterial();
				ScenarioHelpers::SpawnGround(Scene, Context, Material);
				const FBodySetup* CubeSetup = ScenarioHelpers::MakeCubeSetup(Scene, "Cube100", Edge, Material);
				FBodyInstance Template = Context.MakeBodyTemplate();
				Template.bSimulatePhysics = true;
				Template.bOverrideMass = true;
				Template.MassInKgOverride = MassKg;
				ScenarioHelpers::FPerturbation Perturbation(Context);
				for (uint32 Index = 0; Index < N; ++Index)
				{
					Template.BodyName = "TowerBox" + std::to_string(Index);
					const FVector Location(0.0, 0.0, 0.5 * Edge + Edge * static_cast<double>(Index));
					Scene.SpawnBody(Template, CubeSetup, FTransform(Location + Perturbation.Next()));
				}
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FM2aTower, M2aTower)
