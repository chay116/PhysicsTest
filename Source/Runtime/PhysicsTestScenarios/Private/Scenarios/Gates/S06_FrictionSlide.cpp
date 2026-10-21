// S06_FrictionSlide.cpp - S06.FrictionSlide (docs/Scenes.md 2.10, G5). Body 1: a 50 cm cube (10 kg) resting on the ground
// starts at 500 cm/s along +X; cube and ground have friction 0.5 (Average), restitution 0, damping 0. G5: it stops after
// the discrete distance 250.947 cm (dt_engine, Analytic::FrictionSlideStopDistance; the continuous 255.10 cm is wrong,
// DL23) within 1%. Body 2: a static incline 600 x 300 x 20 cm pitched 30 deg about Y (friction 0), centred at
// (0, 1000, 400) cm. Bodies 3 and 4: 50 cm cubes at rest on it, aligned with it, whose material has friction 0.6 / 0.55
// with the Max mode, so the pair friction is theirs (the larger mode wins, the incline is Average): 0.6 >= tan 30 deg holds,
// 0.55 slides at g (sin 30 - 0.55 cos 30) = 23.2 cm/s^2. G5 reads the incline at frame 20, before a sleep timer can stop a
// slow slider. 120 frames.
#include "GateScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FS06FrictionSlide final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return GateScenes::S06::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Gate; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "cube sliding at 500 cm/s with mu 0.5 (stop 250.947 cm), cubes with mu 0.6 / 0.55 on a 30 deg incline (G5)";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return GateScenes::S06::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, static_cast<uint32>(GateScenes::S06::Frames - 1) } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 5, 64, 64 }; }
			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace GateScenes::S06;
				const FPhysicalMaterial* Floor = ScenarioHelpers::MakeMaterial(Scene, Context, "SlideFloor", Friction, 0.0f);
				ScenarioHelpers::SpawnGround(Scene, Context, Floor);
				const FBodySetup* SlideSetup = ScenarioHelpers::MakeCubeSetup(Scene, "Cube50", Edge, Floor);
				FBodyInstance Slider = ScenarioHelpers::MakeGateBodyTemplate(Context, "Slider", MassKg, true);
				Slider.InitialLinearVelocity = FVector(V0, 0.0, 0.0);
				Scene.SpawnBody(Slider, SlideSetup, FTransform(FVector(0.0, 0.0, 0.5 * Edge)));

				// The incline: a static box pitched up by SlopeDegrees (its local X climbs, its top face normal is R * Z).
				const FPhysicalMaterial* InclineMaterial = ScenarioHelpers::MakeMaterial(Scene, Context, "Incline", 0.0f, 0.0f);
				FBodySetup InclineBox;
				InclineBox.BodySetupName = "Incline";
				InclineBox.AggGeom.BoxElems.emplace_back(InclineSizeX, InclineSizeY, InclineSizeZ);
				InclineBox.PhysMaterial = InclineMaterial;
				const FBodySetup* InclineSetup = Scene.CreateBodySetup(std::move(InclineBox));
				FBodyInstance Incline = Context.MakeBodyTemplate();
				Incline.BodyName = "Incline";
				Incline.bStatic = true;
				Incline.bSimulatePhysics = false;
				const FRotator Pitch(SlopeDegrees, 0.0, 0.0);
				const FVector InclineCentre(0.0, InclineCentreY, InclineCentreZ);
				Scene.SpawnBody(Incline, InclineSetup, FTransform(Pitch, InclineCentre));

				// The two cubes rest on the top face, aligned with the incline (no gap, no overlap).
				const FVector Up = FQuat(Pitch).GetAxisZ();
				const float SlopeFriction[2] = { HoldFriction, SlideFriction };
				const char* const Names[2] = { "Hold", "SlopeSlide" };
				for (int32 Index = 0; Index < 2; ++Index)
				{
					FPhysicalMaterial Template = Context.MakeMaterialTemplate();
					Template.Name = Names[Index];
					Template.Friction = SlopeFriction[Index];
					Template.StaticFriction = 0.0f;
					Template.Restitution = 0.0f;
					Template.FrictionCombineMode = EFrictionCombineMode::Max;
					Template.bOverrideFrictionCombineMode = true;
					const FPhysicalMaterial* Material = Scene.CreatePhysicalMaterial(Template);
					const FBodySetup* CubeSetup = ScenarioHelpers::MakeCubeSetup(Scene, Names[Index], Edge, Material);
					const double SideY = Index == 0 ? -SlopeBoxOffsetY : SlopeBoxOffsetY;
					const FVector Centre = InclineCentre + Up * (0.5 * InclineSizeZ + 0.5 * Edge) + FVector(0.0, SideY, 0.0);
					Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, Names[Index], MassKg, true), CubeSetup, FTransform(Pitch, Centre));
				}
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FS06FrictionSlide, S06FrictionSlide)
