// M6a_Pendulum.cpp - M6a.Pendulum(Angle, Joint) (docs/Scenes.md 2.19, 03_WorkPlan M9). A solid sphere bob (radius 5 cm, 1 kg)
// 100 cm below a world pivot, released from rest Angle degrees off the vertical in the XZ plane, on a hinge (default,
// ConstraintUtils::ConfigureAsHinge: the hinge axis is world Y) or a ball joint (ConfigureAsBallAndSocket). Reference: the
// physical pendulum T = 2 pi sqrt((L^2 + 2/5 r^2) / (g L)) x 2 K(sin(Angle / 2)) / pi = 2.00905 / 2.04305 / 2.37023 s for 5 /
// 30 / 90 deg (03 M9: the 5 deg period within 0.5 %). Needs the joint capability of its Joint parameter: a backend without
// it refuses the scene (bench exit 2, status unsupported) and FPhysScene keeps the joint host-only. Deterministic.
#include "BehaviorScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FM6aPendulum final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return BehaviorScenes::M6a::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Behavior; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "sphere bob r 5, 1 kg, 100 cm below a world pivot, released Angle deg off vertical; Joint=Hinge (axis Y) or Ball; 600 frames";
			}
			std::vector<FScenarioParamDesc> GetParamDescs() const override
			{
				return { FScenarioParamDesc{ "Angle", "5", { "5", "30", "90" }, "release angle from the vertical, degrees" },
					FScenarioParamDesc{ "Joint", "Hinge", { "Hinge", "Ball" }, "ConfigureAsHinge (axis world Y) or ConfigureAsBallAndSocket" } };
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return BehaviorScenes::M6a::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, BehaviorScenes::M6a::Frames - 1 } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 1, 64, 64 }; }
			uint64 GetRequiredCapabilities(const FScenarioParams& Resolved) const override
			{
				const auto Joint = Resolved.find("Joint");
				return Joint != Resolved.end() && Joint->second == "Ball" ? PT_CAP_JOINT_BALL : PT_CAP_JOINT_HINGE;
			}

			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace BehaviorScenes::M6a;
				const double Angle = std::stod(Context.GetParam("Angle")) * 3.14159265358979323846 / 180.0;
				const bool bHinge = Context.GetParam("Joint") != "Ball";
				FBodySetup Setup;
				Setup.BodySetupName = "PendulumBob";
				Setup.AggGeom.SphereElems.emplace_back(BobRadius);
				Setup.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
				const FBodySetup* BobSetup = Scene.CreateBodySetup(std::move(Setup));
				FBodyInstance Template = Context.MakeBodyTemplate();
				Template.BodyName = "Bob";
				Template.bSimulatePhysics = true;
				Template.bOverrideMass = true;
				Template.MassInKgOverride = BobMassKg;
				// FRotator(Pitch) maps the bob's local +Z to (-sin a, 0, cos a): from the bob centre toward the pivot.
				const FVector Pivot(0.0, 0.0, PivotZ);
				const FVector BobCentre = Pivot + FVector(std::sin(Angle), 0.0, -std::cos(Angle)) * Length;
				const FRotator BobRotation(Angle * 180.0 / 3.14159265358979323846, 0.0, 0.0);
				FBodyInstance* Bob = Scene.SpawnBody(Template, BobSetup, FTransform(BobRotation, BobCentre));
				// Frames: X = world Y (the hinge axis) on both sides; Y along the rod (bob local +Z), so the frames coincide at rest.
				const FVector RodWorld = BobRotation.Quaternion().RotateVector(FVector(0.0, 0.0, 1.0));
				const FConstraintInstance Joint = ScenarioHelpers::MakeJoint(bHinge ? "PendulumHinge" : "PendulumBall", bHinge,
					FVector(0.0, 0.0, Length), FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0),
					Pivot, FVector(0.0, 1.0, 0.0), RodWorld, false);
				Scene.SpawnConstraint(Joint, Bob, nullptr);
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FM6aPendulum, M6aPendulum)
