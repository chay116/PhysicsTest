// S00d_FreeFallDamped.cpp - S00d.FreeFallDamped (docs/Scenes.md 2.4, G1b). S00.FreeFall with linear damping 0.5 1/s: a
// 1 kg sphere of radius 50 cm released at z = 1000 cm falls for 60 frames of FixedStep 1/60 under gravity -980 cm/s^2.
// No ground. G1b compares z60 with the reference of the damping formula the backend declares (effective key
// body.damping_formula, Analytic::DampedFreeFall) within 1e-3 cm.
#include "GateScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FS00dFreeFallDamped final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return GateScenes::S00d::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Gate; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "S00.FreeFall with linear damping 0.5 1/s: 1 kg sphere r 50 cm from z 1000 cm, 60 frames (G1b)";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return GateScenes::S00d::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, static_cast<uint32>(GateScenes::S00d::Frames - 1) } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 1, 64, 64 }; }
			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace GateScenes::S00d;
				FBodySetup Setup;
				Setup.BodySetupName = "DampedSphere";
				Setup.AggGeom.SphereElems.emplace_back(Radius);
				const FBodySetup* SphereSetup = Scene.CreateBodySetup(std::move(Setup));
				FBodyInstance Template = ScenarioHelpers::MakeGateBodyTemplate(Context, "DampedSphere", MassKg, true);
				Template.LinearDamping = LinearDamping;
				Scene.SpawnBody(Template, SphereSetup, FTransform(FVector(0.0, 0.0, Z0)));
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FS00dFreeFallDamped, S00dFreeFallDamped)
