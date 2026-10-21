// S01_UnitImpulse.cpp - S01.UnitImpulse (docs/Scenes.md 2.5, G2). Two free 50 cm cubes of 10 kg (bOverrideMass), gravity
// off, damping 0, 30 frames. Frame 0's pre-tick: body 0 gets AddImpulse((1000, 0, 0) kg cm/s) and body 1 the velocity
// change AddImpulse((100, 0, 0) cm/s, bVelChange) (FBodyInstance::AddImpulse -> ADD_IMPULSE / ADD_VELOCITY,
// BodyInstance.cpp:3875-3888 [V]). Both move at 100 cm/s from step 1 on (G2: 1e-4 relative), and their mass
// properties read back from the engine within 1e-5.
#include "GateScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FS01UnitImpulse final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return GateScenes::S01::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Gate; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "two free 10 kg 50 cm cubes, gravity off: impulse 1000 kg cm/s and velocity change 100 cm/s at frame 0 (G2)";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return GateScenes::S01::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, static_cast<uint32>(GateScenes::S01::Frames - 1) } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 2, 64, 64 }; }
			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace GateScenes::S01;
				const FPhysicalMaterial* Material = ScenarioHelpers::MakeMaterial(Scene, Context, "UnitImpulse", 0.5f, 0.0f);
				const FBodySetup* CubeSetup = ScenarioHelpers::MakeCubeSetup(Scene, "Cube50", Edge, Material);
				Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, "Impulse", MassKg, false), CubeSetup, FTransform(FVector(0.0, 0.0, 500.0)));
				Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, "VelocityChange", MassKg, false), CubeSetup, FTransform(FVector(0.0, 300.0, 500.0)));
				Scene.FlushDeferredActors();
			}
			void OnPreTick(FPhysScene& Scene, float /*DeltaSeconds*/) override
			{
				using namespace GateScenes::S01;
				if (Scene.GetFrameCount() != 0 || Scene.GetBodies().size() < 2)
				{
					return;
				}
				Scene.GetBodies()[ImpulseBody]->AddImpulse(FVector(Impulse, 0.0, 0.0), false);
				Scene.GetBodies()[VelocityChangeBody]->AddImpulse(FVector(VelocityChange, 0.0, 0.0), true);
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FS01UnitImpulse, S01UnitImpulse)
