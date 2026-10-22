// M4_InclineFriction.cpp - M4.InclineFriction(Mu, BoxYaw) (docs/Scenes.md 2.17, 03_WorkPlan M9; DL34: the catalog name). A
// static 30 deg incline and a 50 cm cube of 10 kg resting on it, released from rest; incline and cube share one material
// (F = Mu, SF 0, R 0) so the pair's friction is Mu under every combine mode the profiles use. Mu 0.30 / 0.50 / 0.55 slide
// (a = g (sin 30 - Mu cos 30); the criterion compares d(2 s) with the discrete reference within 1 %), 0.60 / 0.70 hold
// (drift < 0.1 cm; PhysX PGS creeps, docs/Progress.md M6 D-4). BoxYaw 45 is the isotropy variant (the cube turned about
// the incline normal). Deterministic: per-body comparison (02 13).
#include "BehaviorScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FM4InclineFriction final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return BehaviorScenes::M4::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Behavior; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "50 cm cube, 10 kg, at rest on a static 30 deg incline, shared material F = Mu, R 0; 180 frames; BoxYaw 45 = isotropy variant";
			}
			std::vector<FScenarioParamDesc> GetParamDescs() const override
			{
				return { FScenarioParamDesc{ "Mu", "0.50", { "0.30", "0.50", "0.55", "0.60", "0.70" }, "friction of the shared material (tan 30 = 0.577)" },
					FScenarioParamDesc{ "BoxYaw", "0", { "0", "45" }, "degrees the cube is turned about the incline normal (45: isotropy variant)" } };
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return BehaviorScenes::M4::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				// "2s" = frames 1-119: frame f holds the state after step f + 1, so d(2 s) (120 steps, 03 M9) is frame 119's.
				return { FStepWindow{ "headline", 1, BehaviorScenes::M4::Frames - 1 }, FStepWindow{ "2s", 1, 119 } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 2, 64, 64 }; }

			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace BehaviorScenes::M4;
				const float Mu = std::stof(Context.GetParam("Mu"));
				const double BoxYaw = std::stod(Context.GetParam("BoxYaw"));
				FPhysicalMaterial MaterialTemplate = Context.MakeMaterialTemplate();
				MaterialTemplate.Name = "InclineFriction";
				MaterialTemplate.Friction = Mu;
				MaterialTemplate.StaticFriction = 0.0f;   // UE default (DL41); Normalized's forced SF := F leaves the pair at Mu
				MaterialTemplate.Restitution = 0.0f;
				const FPhysicalMaterial* Material = Scene.CreatePhysicalMaterial(MaterialTemplate);

				// The incline: a static box pitched up about Y; its local +X runs up the slope, local +Z is the surface normal.
				const FQuat InclineRotation = FRotator(AngleDegrees, 0.0, 0.0).Quaternion();
				const FVector InclineCentre(0.0, 0.0, InclineCentreZ);
				{
					FBodySetup Setup;
					Setup.BodySetupName = "Incline";
					Setup.AggGeom.BoxElems.emplace_back(InclineLength, InclineWidth, InclineThickness);
					Setup.PhysMaterial = Material;
					const FBodySetup* InclineSetup = Scene.CreateBodySetup(std::move(Setup));
					FBodyInstance Template = Context.MakeBodyTemplate();
					Template.BodyName = "Incline";
					Template.bStatic = true;
					Template.bSimulatePhysics = false;
					Scene.SpawnBody(Template, InclineSetup, FTransform(InclineRotation, InclineCentre));
				}
				// The cube: on the top face, StartFromUpperEnd below the upper end, turned BoxYaw about the incline normal.
				const FVector LocalCentre(0.5 * InclineLength - StartFromUpperEnd, 0.0, 0.5 * InclineThickness + 0.5 * Edge);
				const FQuat CubeRotation = (InclineRotation * FQuat(FVector(0.0, 0.0, 1.0), BoxYaw * 3.14159265358979323846 / 180.0)).GetNormalized();
				const FBodySetup* CubeSetup = ScenarioHelpers::MakeCubeSetup(Scene, "Cube50", Edge, Material);
				FBodyInstance Template = Context.MakeBodyTemplate();
				Template.BodyName = "SlidingCube";
				Template.bSimulatePhysics = true;
				Template.bOverrideMass = true;
				Template.MassInKgOverride = MassKg;
				Scene.SpawnBody(Template, CubeSetup, FTransform(CubeRotation, InclineCentre + InclineRotation.RotateVector(LocalCentre)));
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FM4InclineFriction, M4InclineFriction)
