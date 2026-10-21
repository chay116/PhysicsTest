// S07_ClampProbe.cpp - S07.ClampProbe (docs/Scenes.md 2.11, G7). Three free 100 cm cubes (100 kg, isotropic inertia, so
// no precession), gravity off, damping 0, start spinning about Z at 50 / 80 / 120 rad/s (InitialAngularVelocityInRadians,
// the harness extension). The angular limit is the profile's (not pinned): UEParity 3600 deg/s = 62.83 rad/s
// (UPhysicsSettingsCore::MaxAngularVelocity), Normalized released (1e7 deg/s). 10 frames. G7: UEParity 50 / 62.83 / 62.83
// within 0.01 rad/s, Normalized 50 / 80 / 120. Bullet turns a body by less than |w| dt above pi/4 per step (47.1 rad/s at
// 60 Hz) without changing the velocity - the registered exception (DL16, tolerances.json).
#include "GateScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FS07ClampProbe final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return GateScenes::S07::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Gate; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "free 100 cm cubes spinning at 50 / 80 / 120 rad/s about Z under the profile's angular limit (G7)";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return GateScenes::S07::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, static_cast<uint32>(GateScenes::S07::Frames - 1) } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 3, 64, 64 }; }
			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace GateScenes::S07;
				const FPhysicalMaterial* Material = ScenarioHelpers::MakeMaterial(Scene, Context, "ClampProbe", 0.5f, 0.0f);
				const FBodySetup* CubeSetup = ScenarioHelpers::MakeCubeSetup(Scene, "Cube100", Edge, Material);
				for (int32 Index = 0; Index < 3; ++Index)
				{
					FBodyInstance Template = ScenarioHelpers::MakeGateBodyTemplate(Context, Index == 0 ? "Spin50" : (Index == 1 ? "Spin80" : "Spin120"), MassKg, false);
					Template.InitialAngularVelocityInRadians = FVector(0.0, 0.0, Omega0[Index]);
					Scene.SpawnBody(Template, CubeSetup, FTransform(FVector(300.0 * Index, 0.0, 500.0)));
				}
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FS07ClampProbe, S07ClampProbe)
