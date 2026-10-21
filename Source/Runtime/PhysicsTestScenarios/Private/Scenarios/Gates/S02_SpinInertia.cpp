// S02_SpinInertia.cpp - S02.SpinInertia (docs/Scenes.md 2.6, G2). Free 100 x 50 x 20 cm boxes of 10 kg (bOverrideMass),
// gravity off, damping 0, 60 frames. Frame 0's pre-tick gives each an angular impulse (AddAngularImpulseInRadians):
// body 0 about Z (Izz = 10416.7 kg cm^2), body 1 about X (Ixx = 2416.7 kg cm^2), body 2 = body 0 with COMNudge
// (0, 0, 10) cm, so its COM is 10 cm above the box centre and every engine must place the shape around it (the
// COM-wrapping path, DL42). omega = I^-1 L = 2 rad/s about the axis (G2: 1e-4 relative); the nudged body spins about its
// COM, which stays in place.
#include "GateScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FS02SpinInertia final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return GateScenes::S02::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Gate; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "free 100x50x20 cm 10 kg boxes, gravity off: angular impulse about Z, about X, and about Z with COMNudge (0,0,10) (G2)";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return GateScenes::S02::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, static_cast<uint32>(GateScenes::S02::Frames - 1) } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 3, 64, 64 }; }
			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace GateScenes::S02;
				const FPhysicalMaterial* Material = ScenarioHelpers::MakeMaterial(Scene, Context, "SpinInertia", 0.5f, 0.0f);
				FBodySetup Setup;
				Setup.BodySetupName = "Box100x50x20";
				Setup.AggGeom.BoxElems.emplace_back(SizeX, SizeY, SizeZ);
				Setup.PhysMaterial = Material;
				const FBodySetup* BoxSetup = Scene.CreateBodySetup(std::move(Setup));
				Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, "SpinZ", MassKg, false), BoxSetup, FTransform(FVector(0.0, 0.0, 500.0)));
				Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, "SpinX", MassKg, false), BoxSetup, FTransform(FVector(0.0, 300.0, 500.0)));
				FBodyInstance Nudged = ScenarioHelpers::MakeGateBodyTemplate(Context, "SpinZNudged", MassKg, false);
				Nudged.COMNudge = FVector(0.0, 0.0, ComNudgeZ);
				Scene.SpawnBody(Nudged, BoxSetup, FTransform(FVector(0.0, 600.0, 500.0)));
				Scene.FlushDeferredActors();
			}
			void OnPreTick(FPhysScene& Scene, float /*DeltaSeconds*/) override
			{
				using namespace GateScenes::S02;
				if (Scene.GetFrameCount() != 0 || Scene.GetBodies().size() < 3)
				{
					return;
				}
				// L = I omega about a principal axis with the harness moments (the unrotated box's principal frame is its own).
				Scene.GetBodies()[SpinZBody]->AddAngularImpulseInRadians(FVector(0.0, 0.0, Izz * TargetOmega), false);
				Scene.GetBodies()[SpinXBody]->AddAngularImpulseInRadians(FVector(Ixx * TargetOmega, 0.0, 0.0), false);
				Scene.GetBodies()[NudgedBody]->AddAngularImpulseInRadians(FVector(0.0, 0.0, Izz * TargetOmega), false);
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FS02SpinInertia, S02SpinInertia)
