// M1_SingleBoxDrop.cpp - M1.SingleBoxDrop (docs/Scenes.md, 03_WorkPlan M3). One 100 cm cube, 1000 kg (bOverrideMass),
// centre at z = 150 cm (its bottom 100 cm above the ground), dropped on the static ground box; material mu 0.5, e 0 on
// both; 300 frames. Case a: axis-aligned (flat landing). Case b: roll 30 deg then pitch 20 deg (FRotator(20, 0, 30):
// UE applies Roll first, UnrealMath.cpp:483), so it lands on a corner and tips over. A deterministic scene used for
// per-body comparisons (02 13: M1.SingleBoxDrop) and for the resting-contact snapshot check (03 M3: normal . +Z > 0.999,
// -2.5 cm <= Phi <= 0.5 cm for case a). M9 adds the behaviour window "fall" (the frames before the first contact).
#include "BehaviorScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FM1SingleBoxDrop final : public IPhysicsTestScenario
		{
		public:
			static constexpr double Edge = 100.0;        // cm
			static constexpr float MassKg = 1000.0f;
			static constexpr double StartZ = 150.0;      // cm, box centre
			static constexpr float Friction = 0.5f;
			static constexpr float Restitution = 0.0f;
			static constexpr int32 Frames = 300;

			const char* GetName() const override { return "M1.SingleBoxDrop"; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Behavior; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "100 cm cube, 1000 kg, centre z 150 cm onto the ground box, mu 0.5, e 0, 300 frames; Case=a flat, Case=b roll 30 then pitch 20";
			}
			std::vector<FScenarioParamDesc> GetParamDescs() const override
			{
				return { FScenarioParamDesc{ "Case", "a", { "a", "b" }, "a: axis-aligned; b: roll 30 deg then pitch 20 deg" } };
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& Params) const override
			{
				// M9 behaviour window "fall": the free fall before the first contact (BehaviorScenes::M1), where only damping
				// separates the profiles (03 M9: the UEParity vs Normalized table).
				const auto Case = Params.find("Case");
				const bool bCaseB = Case != Params.end() && Case->second == "b";
				return { FStepWindow{ "headline", 1, Frames - 1 }, FStepWindow{ "impact", 1, 89 }, FStepWindow{ "settled", 150, Frames - 1 },
					FStepWindow{ "fall", 1, bCaseB ? BehaviorScenes::M1::FallLastFrameB : BehaviorScenes::M1::FallLastFrameA } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 2, 64, 64 }; }

			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				FPhysicalMaterial MaterialTemplate = Context.MakeMaterialTemplate();
				MaterialTemplate.Name = "SingleBoxDrop";
				MaterialTemplate.Friction = Friction;
				MaterialTemplate.StaticFriction = 0.0f;   // UE default: the dynamic value is the pair's static value too (DL41)
				MaterialTemplate.Restitution = Restitution;
				const FPhysicalMaterial* Material = Scene.CreatePhysicalMaterial(MaterialTemplate);

				ScenarioHelpers::SpawnGround(Scene, Context, Material);

				const FBodySetup* CubeSetup = ScenarioHelpers::MakeCubeSetup(Scene, "Cube100", Edge, Material);
				FBodyInstance Template = Context.MakeBodyTemplate();
				Template.BodyName = "Box";
				Template.bSimulatePhysics = true;
				Template.bOverrideMass = true;
				Template.MassInKgOverride = MassKg;
				const bool bCaseB = Context.GetParam("Case") == "b";
				const FRotator Rotation = bCaseB ? FRotator(20.0, 0.0, 30.0) : FRotator(0.0, 0.0, 0.0);
				Scene.SpawnBody(Template, CubeSetup, FTransform(Rotation, FVector(0.0, 0.0, StartZ)));
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FM1SingleBoxDrop, M1SingleBoxDrop)
