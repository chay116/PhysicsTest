#include "BackendJolt.h"

#include <Jolt/Jolt.h>
#include <Jolt/ConfigurationString.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

JPH_SUPPRESS_WARNINGS_STD_BEGIN
#include <algorithm>
#include <chrono>
#include <vector>
JPH_SUPPRESS_WARNINGS_STD_END

using namespace JPH;

namespace
{
	class OLPF final : public ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override { return a == 1 || b == 1; }
	};
	class BPLI final : public BroadPhaseLayerInterface
	{
	public:
		uint GetNumBroadPhaseLayers() const override { return 2; }
		BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l) const override { return BroadPhaseLayer(BroadPhaseLayer::Type(l)); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char *GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "?"; }
#endif
	};
	class OVBPF final : public ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(ObjectLayer l, BroadPhaseLayer b) const override { return l == 1 || b.GetValue() == 1; }
	};
}

PTDropResult PT_JoltDropBox(int inNumSteps, int inNumWorkers)
{
	RegisterDefaultAllocator();
	Factory::sInstance = new Factory();
	RegisterTypes();

	PTDropResult r{};
	{
		TempAllocatorImpl temp(10 * 1024 * 1024);
		JobSystemThreadPool jobs(cMaxPhysicsJobs, cMaxPhysicsBarriers, inNumWorkers);
		BPLI bpli; OVBPF ovbpf; OLPF olpf;
		PhysicsSystem ps;
		ps.Init(1024, 0, 1024, 1024, bpli, ovbpf, olpf);
		BodyInterface &bi = ps.GetBodyInterface();
		bi.CreateAndAddBody(BodyCreationSettings(new BoxShape(Vec3(100, 1, 100)), RVec3(0, -1, 0), Quat::sIdentity(), EMotionType::Static, 0), EActivation::DontActivate);
		BodyID box = bi.CreateAndAddBody(BodyCreationSettings(new BoxShape(Vec3::sReplicate(0.5f)), RVec3(0, 10, 0), Quat::sIdentity(), EMotionType::Dynamic, 1), EActivation::Activate);
		std::vector<double> us;
		for (int i = 0; i < inNumSteps; ++i)
		{
			auto t0 = std::chrono::steady_clock::now();
			ps.Update(1.0f / 60.0f, 1, &temp, &jobs);
			us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
		}
		std::sort(us.begin(), us.end());
		r.FinalY = double(bi.GetCenterOfMassPosition(box).GetY());
		r.MedianStepUs = us[us.size() / 2];
		r.Config = GetConfigurationString();
	}
	UnregisterTypes();
	delete Factory::sInstance;
	Factory::sInstance = nullptr;
	return r;
}
