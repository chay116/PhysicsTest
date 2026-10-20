// Minimal Jolt smoke test: 1 box dropped from z=10 (Z-up like UE), 120 steps @ 60 Hz
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <cstdio>
#include <thread>
using namespace JPH;
namespace L { constexpr ObjectLayer STATIC = 0, MOVING = 1, NUM = 2; }
struct BPL final : BroadPhaseLayerInterface {
  uint GetNumBroadPhaseLayers() const override { return 2; }
  BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l) const override { return BroadPhaseLayer(BroadPhaseLayer::Type(l)); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "?"; }
#endif
};
struct OvB : ObjectVsBroadPhaseLayerFilter { bool ShouldCollide(ObjectLayer a, BroadPhaseLayer b) const override { return a == L::MOVING || b.GetValue() == L::MOVING; } };
struct OvO : ObjectLayerPairFilter { bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override { return a == L::MOVING || b == L::MOVING; } };
int main() {
  RegisterDefaultAllocator(); Factory::sInstance = new Factory(); RegisterTypes();
  { TempAllocatorImpl temp(10 << 20);
    JobSystemThreadPool jobs(cMaxPhysicsJobs, cMaxPhysicsBarriers, int(std::thread::hardware_concurrency()) - 1);
    BPL bpl; OvB ovb; OvO ovo; PhysicsSystem ps; ps.Init(1024, 0, 1024, 1024, bpl, ovb, ovo);
    ps.SetGravity(Vec3(0, 0, -9.81f));
    BodyInterface& bi = ps.GetBodyInterface();
    bi.CreateAndAddBody(BodyCreationSettings(new BoxShape(Vec3(50, 50, 1)), RVec3(0, 0, -1), Quat::sIdentity(), EMotionType::Static, L::STATIC), EActivation::DontActivate);
    BodyID box = bi.CreateAndAddBody(BodyCreationSettings(new BoxShape(Vec3(0.5f, 0.5f, 0.5f)), RVec3(0, 0, 10), Quat::sIdentity(), EMotionType::Dynamic, L::MOVING), EActivation::Activate);
    ps.OptimizeBroadPhase();
    for (int i = 0; i < 120; ++i) ps.Update(1.0f / 60.0f, 1, &temp, &jobs);
    RVec3 p = bi.GetCenterOfMassPosition(box);
    std::printf("[jolt] after 2s: z=%.4f  (AVX2 compiled: %s)\n", double(p.GetZ()),
#ifdef JPH_USE_AVX2
      "yes"
#else
      "NO"
#endif
    );
  }
  UnregisterTypes(); delete Factory::sInstance; Factory::sInstance = nullptr;
}
