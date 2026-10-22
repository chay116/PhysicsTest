// M5_BouncingBalls.cpp - M5.BouncingBalls (docs/Scenes.md 2.18, 03_WorkPlan M9). Four spheres (radius 25 cm, 10 kg) of
// restitution 0.3 / 0.5 / 0.7 / 0.9, each over its own static pad of the same material (the pair's e is the ball's), dropped
// from the G4 step-boundary height (K = 38) + 0.01 cm (as S05 v2): the first impact is in step 39 at 620.67 cm/s for every
// engine; the analysis measures e_k = -v_n(c) / v_n(c - 1) at every impact above the restitution threshold (03 M9: within the
// engine's registered tolerance, default +-0.02). Pads 0-3, balls 4-7. Deterministic: per-body comparison (02 13).
#include "BehaviorScenes.h"
#include "Physics/AnalyticReference.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FM5BouncingBalls final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return BehaviorScenes::M5::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Behavior; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "spheres r 25, 10 kg, e 0.3 / 0.5 / 0.7 / 0.9 on pads of the same material, dropped from the step-boundary height (K 38) + 0.01 cm; 600 frames";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return BehaviorScenes::M5::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				// "impact1" = around the first impact (step 39), as S05's impact window.
				return { FStepWindow{ "headline", 1, BehaviorScenes::M5::Frames - 1 }, FStepWindow{ "impact1", 36, 42 } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override
			{
				return FScenarioCapacity{ 2 * BehaviorScenes::M5::NumBalls, 64, 64 };
			}

			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace BehaviorScenes::M5;
				// The drop height from the scene's own step: the tick mode's dt and the profile's gravity (as S05).
				const double G = -Scene.GetGravity().Z;
				const double H0 = Analytic::StepBoundaryDropHeight(G, Analytic::EngineDeltaTime(Scene.GetTestSettings().FixedDeltaTime), TouchStep);
				const FPhysicalMaterial* Materials[NumBalls] = {};
				for (int32 Index = 0; Index < NumBalls; ++Index)
				{
					const std::string MaterialName = "Bounce" + std::to_string(Index);
					Materials[Index] = ScenarioHelpers::MakeMaterial(Scene, Context, MaterialName.c_str(), 0.5f, Restitutions[Index]);
				}
				// Pads first (static, bodies 0-3), each with its ball's material.
				for (int32 Index = 0; Index < NumBalls; ++Index)
				{
					FBodySetup Setup;
					Setup.BodySetupName = "Pad" + std::to_string(Index);
					Setup.AggGeom.BoxElems.emplace_back(2.0 * PadHalfXY, 2.0 * PadHalfXY, 2.0 * PadHalfZ);
					Setup.PhysMaterial = Materials[Index];
					const FBodySetup* PadSetup = Scene.CreateBodySetup(std::move(Setup));
					FBodyInstance Template = Context.MakeBodyTemplate();
					Template.BodyName = "Pad" + std::to_string(Index);
					Template.bStatic = true;
					Template.bSimulatePhysics = false;
					Scene.SpawnBody(Template, PadSetup, FTransform(FVector(FirstX + SpacingX * Index, 0.0, -PadHalfZ)));
				}
				// Balls 4-7: damping 0 (the e reference assumes none; the profile still decides the restitution threshold and
				// the angular limit).
				for (int32 Index = 0; Index < NumBalls; ++Index)
				{
					FBodySetup Setup;
					Setup.BodySetupName = "Ball" + std::to_string(Index);
					Setup.AggGeom.SphereElems.emplace_back(Radius);
					Setup.PhysMaterial = Materials[Index];
					const FBodySetup* BallSetup = Scene.CreateBodySetup(std::move(Setup));
					const std::string BallName = "Ball" + std::to_string(Index);
					Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, BallName.c_str(), MassKg, true), BallSetup,
						FTransform(FVector(FirstX + SpacingX * Index, 0.0, Radius + H0 + TouchGap)));
				}
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FM5BouncingBalls, M5BouncingBalls)
