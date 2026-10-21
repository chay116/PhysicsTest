// S05_RestitutionCombine.cpp - S05.RestitutionCombine (docs/Scenes.md 2.9, G4). Three spheres (radius 25 cm, 10 kg,
// restitution 0.8, damping 0) fall from rest onto a ground of restitution 0. Their bottoms start
// h0 = g dt_engine^2 K (K + 1) / 2 + TouchGap above the ground (K = 38: 201.7167 + 0.01 cm at 60 Hz,
// Analytic::StepBoundaryDropHeight), so the discrete trajectory ends step 38 0.01 cm above the ground at 620.67 cm/s and
// no engine starts the contact early or deep (DL23, 02 14.3; TouchGap: GateScenes.h, version 2). The pair's restitution mode is the larger of the two materials' (the ground is Average):
// body 1 Average (e 0.4), body 2 Multiply (e 0), body 3 Max (e 0.8). The profile's restitution threshold applies
// (Normalized 50 cm/s; UEParity the engine mapping, at most 200 cm/s - all below the impact speed). 120 frames.
#include "GateScenes.h"
#include "Physics/AnalyticReference.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FS05RestitutionCombine final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return GateScenes::S05::Name; }
			int32 GetVersion() const override { return 2; }   // 2: TouchGap (M6 round 2)
			EScenarioKind GetKind() const override { return EScenarioKind::Gate; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "spheres e 0.8 onto a ground of e 0 from the step-boundary height (K 38) + 0.01 cm: Average 0.4, Multiply 0, Max 0.8 (G4)";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return GateScenes::S05::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, static_cast<uint32>(GateScenes::S05::Frames - 1) }, FStepWindow{ "impact", 36, 42 } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 4, 64, 64 }; }
			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace GateScenes::S05;
				const FPhysicalMaterial* Ground = ScenarioHelpers::MakeMaterial(Scene, Context, "GroundNoBounce", 0.5f, 0.0f);
				ScenarioHelpers::SpawnGround(Scene, Context, Ground);
				// The drop height from the scene's own step: the frame dt of the tick mode and gravity of the profile.
				const FPhysicsTestSettings& Test = Scene.GetTestSettings();
				const double G = -Scene.GetGravity().Z;
				const double H0 = Analytic::StepBoundaryDropHeight(G, Analytic::EngineDeltaTime(Test.FixedDeltaTime), TouchStep);
				const EFrictionCombineMode Modes[3] = { EFrictionCombineMode::Average, EFrictionCombineMode::Multiply, EFrictionCombineMode::Max };
				const char* const Names[3] = { "BallAverage", "BallMultiply", "BallMax" };
				for (int32 Index = 0; Index < 3; ++Index)
				{
					FPhysicalMaterial Template = Context.MakeMaterialTemplate();
					Template.Name = Names[Index];
					Template.Friction = 0.5f;
					Template.StaticFriction = 0.0f;
					Template.Restitution = Restitution;
					Template.RestitutionCombineMode = Modes[Index];
					Template.bOverrideRestitutionCombineMode = true;
					const FPhysicalMaterial* Material = Scene.CreatePhysicalMaterial(Template);
					FBodySetup Setup;
					Setup.BodySetupName = Names[Index];
					Setup.AggGeom.SphereElems.emplace_back(Radius);
					Setup.PhysMaterial = Material;
					const FBodySetup* SphereSetup = Scene.CreateBodySetup(std::move(Setup));
					Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, Names[Index], MassKg, true), SphereSetup,
						FTransform(FVector(-200.0 + 200.0 * Index, 0.0, Radius + H0 + TouchGap)));
				}
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FS05RestitutionCombine, S05RestitutionCombine)
