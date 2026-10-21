// S03_CapsuleRest.cpp - S03.CapsuleRest (docs/Scenes.md 2.7, G2). Two capsules of radius 25 cm and length 100 cm
// (FKSphylElem::Length, the segment; HalfLength 50) placed exactly on the ground: body 1 standing (axis Z, centre
// z = 75 cm), body 2 lying (axis along Y, centre z = 25 cm). Friction 0.5, restitution 0, damping 0, 50 kg, 120 frames.
// G2: the centres stay at 75 / 25 cm within 0.5 cm - the capsule axis convention (UE Z -> PhysX X through U2PSphylBasis,
// Jolt Y, Bullet btCapsuleShapeZ; D1 / DL47) and the HalfLength + Radius height.
#include "GateScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FS03CapsuleRest final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return GateScenes::S03::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Gate; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "capsules R 25 L 100 resting on the ground: standing (centre 75 cm) and lying (centre 25 cm), 120 frames (G2)";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return GateScenes::S03::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, static_cast<uint32>(GateScenes::S03::Frames - 1) } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 3, 64, 64 }; }
			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				using namespace GateScenes::S03;
				const FPhysicalMaterial* Material = ScenarioHelpers::MakeMaterial(Scene, Context, "CapsuleRest", 0.5f, 0.0f);
				ScenarioHelpers::SpawnGround(Scene, Context, Material);
				FBodySetup Setup;
				Setup.BodySetupName = "Capsule25x100";
				Setup.AggGeom.SphylElems.emplace_back(Radius, Length);
				Setup.PhysMaterial = Material;
				const FBodySetup* CapsuleSetup = Scene.CreateBodySetup(std::move(Setup));
				Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, "Standing", MassKg, true), CapsuleSetup, FTransform(FVector(0.0, 0.0, StandingZ)));
				// Roll 90 deg turns the local Z axis onto world -Y: the capsule lies along Y.
				Scene.SpawnBody(ScenarioHelpers::MakeGateBodyTemplate(Context, "Lying", MassKg, true), CapsuleSetup,
					FTransform(FRotator(0.0, 0.0, 90.0), FVector(300.0, 0.0, LyingZ)));
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FS03CapsuleRest, S03CapsuleRest)
