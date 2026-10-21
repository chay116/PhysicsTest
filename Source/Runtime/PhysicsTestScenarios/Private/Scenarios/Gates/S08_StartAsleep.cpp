// S08_StartAsleep.cpp - S08.StartAsleep (docs/Scenes.md 2.12, G6; DL38). Two 100 cm cubes (100 kg, damping 0) at
// z = 500 cm over the ground: body 1 with bStartAwake = false, body 2 awake. The creation order puts body 1 to sleep after
// AddActorsToScene (02 6 step 5, BodyInstance.cpp:4723-4733 [V]; PhysX putToSleep needs the actor in a scene,
// NpRigidDynamic.cpp:527 [V]). G6: body 1 is Sleeping in the first state read (right after the flush) and after 60 frames,
// at the same height; body 2 falls. Only meaningful with sleeping allowed: UEParity only (RequiresSleeping; Normalized
// skips step 5, 02 6).
#include "GateScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FS08StartAsleep final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return GateScenes::S08::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Gate; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "two cubes at z 500 cm, one with bStartAwake = false: it is asleep from creation and stays; the other falls (G6, UEParity)";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return GateScenes::S08::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, static_cast<uint32>(GateScenes::S08::Frames - 1) } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 3, 64, 64 }; }
			bool RequiresSleeping() const override { return true; }
			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace GateScenes::S08;
				const FPhysicalMaterial* Material = ScenarioHelpers::MakeMaterial(Scene, Context, "StartAsleep", 0.5f, 0.0f);
				ScenarioHelpers::SpawnGround(Scene, Context, Material);
				const FBodySetup* CubeSetup = ScenarioHelpers::MakeCubeSetup(Scene, "Cube100", Edge, Material);
				FBodyInstance Asleep = ScenarioHelpers::MakeGateBodyTemplate(Context, "Asleep", MassKg, true);
				Asleep.bStartAwake = false;
				Scene.SpawnBody(Asleep, CubeSetup, FTransform(FVector(-200.0, 0.0, Z0)));
				Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, "Awake", MassKg, true), CubeSetup, FTransform(FVector(200.0, 0.0, Z0)));
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FS08StartAsleep, S08StartAsleep)
