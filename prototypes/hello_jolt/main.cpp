// Minimal Jolt 5.6.0 hello test for PhysicsTest: ground box + dynamic box dropped from 10 m,
// 60 steps of 1/60 s, prints trajectory, per-step wall time and the defaults that matter for cross-engine comparison.

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
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
JPH_SUPPRESS_WARNINGS_STD_END

using namespace JPH;
using namespace JPH::literals; // _r suffix for Real (float or double depending on JPH_DOUBLE_PRECISION)

static void TraceImpl(const char *inFMT, ...)
{
	va_list list;
	va_start(list, inFMT);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), inFMT, list);
	va_end(list);
	std::printf("[Jolt] %s\n", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, uint inLine)
{
	std::printf("[Jolt assert] %s:%u: (%s) %s\n", inFile, inLine, inExpression, inMessage != nullptr ? inMessage : "");
	return true;
}
#endif

// ---- Layers (same scheme as Jolt HelloWorld / PerformanceTest Layers.h) ----
namespace Layers
{
	static constexpr ObjectLayer NON_MOVING = 0;
	static constexpr ObjectLayer MOVING = 1;
	static constexpr ObjectLayer NUM_LAYERS = 2;
}

class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter
{
public:
	bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
	{
		switch (inObject1)
		{
		case Layers::NON_MOVING:	return inObject2 == Layers::MOVING;
		case Layers::MOVING:		return true;
		default:					JPH_ASSERT(false); return false;
		}
	}
};

namespace BroadPhaseLayers
{
	static constexpr BroadPhaseLayer NON_MOVING(0);
	static constexpr BroadPhaseLayer MOVING(1);
	static constexpr uint NUM_LAYERS(2);
}

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
{
public:
	BPLayerInterfaceImpl()
	{
		mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
		mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
	}
	uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
	BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override { JPH_ASSERT(inLayer < Layers::NUM_LAYERS); return mObjectToBroadPhase[inLayer]; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char *GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override { return inLayer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING"; }
#endif
private:
	BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public ObjectVsBroadPhaseLayerFilter
{
public:
	bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
	{
		switch (inLayer1)
		{
		case Layers::NON_MOVING:	return inLayer2 == BroadPhaseLayers::MOVING;
		case Layers::MOVING:		return true;
		default:					JPH_ASSERT(false); return false;
		}
	}
};

int main(int argc, char **argv)
{
	// Worker thread count for JobSystemThreadPool (the calling thread also executes jobs).
	const int num_workers = argc > 1 ? std::atoi(argv[1]) : int(std::thread::hardware_concurrency()) - 1;
	// Optional 2nd arg "cm": the identical scene authored in centimeters (UE units), gravity scaled to -981,
	// every other Jolt default (PhysicsSettings, BodyCreationSettings, convex radius, density) left untouched.
	const bool cm = argc > 2 && std::strcmp(argv[2], "cm") == 0;
	const float U = cm ? 100.0f : 1.0f;
	const char *un = cm ? "cm" : "m";

	// 1) Library init: allocator hooks, factory, type registration (RegisterTypes also verifies JPH_VERSION_ID / defines)
	RegisterDefaultAllocator();
	Trace = TraceImpl;
	JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)
	Factory::sInstance = new Factory();
	RegisterTypes();
	std::printf("Jolt %d.%d.%d | %s\n", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH, GetConfigurationString());
	std::printf("VerifyJoltVersionID() = %s, workers = %d, units = %s\n", VerifyJoltVersionID() ? "true" : "false", num_workers, un);

	// 2) Temp allocator + job system
	TempAllocatorImpl temp_allocator(10 * 1024 * 1024);
	JobSystemThreadPool job_system(cMaxPhysicsJobs, cMaxPhysicsBarriers, num_workers);

	// 3) Physics system with layer interfaces (must outlive PhysicsSystem)
	BPLayerInterfaceImpl broad_phase_layer_interface;
	ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_layer_filter;
	ObjectLayerPairFilterImpl object_vs_object_layer_filter;

	PhysicsSystem physics_system;
	physics_system.Init(/*maxBodies*/ 1024, /*numBodyMutexes*/ 0, /*maxBodyPairs*/ 1024, /*maxContactConstraints*/ 1024,
		broad_phase_layer_interface, object_vs_broadphase_layer_filter, object_vs_object_layer_filter);

	physics_system.SetGravity(Vec3(0.0f, -9.81f * U, 0.0f));
	const PhysicsSettings &ps = physics_system.GetPhysicsSettings();
	const Vec3 g = physics_system.GetGravity();
	std::printf("gravity=(%g,%g,%g) velSteps=%u posSteps=%u baumgarte=%g specDist=%g penSlop=%g linCastThr=%g sleepT=%g sleepVel=%g allowSleep=%d manifoldRed=%d deterministic=%d\n",
		g.GetX(), g.GetY(), g.GetZ(), ps.mNumVelocitySteps, ps.mNumPositionSteps, ps.mBaumgarte, ps.mSpeculativeContactDistance,
		ps.mPenetrationSlop, ps.mLinearCastThreshold, ps.mTimeBeforeSleep, ps.mPointVelocitySleepThreshold,
		ps.mAllowSleeping, ps.mUseManifoldReduction, ps.mDeterministicSimulation);

	BodyInterface &bi = physics_system.GetBodyInterface();

	// 4) Ground: 200 x 2 x 200 m static box, top face at y = 0
	BodyCreationSettings ground_settings(new BoxShape(Vec3(100.0f, 1.0f, 100.0f) * U), RVec3(0.0_r, Real(-1.0f * U), 0.0_r), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
	BodyID ground_id = bi.CreateAndAddBody(ground_settings, EActivation::DontActivate);

	// 5) Dynamic 1 m cube, center at y = 10 m (all defaults: density 1000 -> 1000 kg, friction 0.2, restitution 0, damping 0.05)
	BodyCreationSettings box_settings(new BoxShape(Vec3(0.5f, 0.5f, 0.5f) * U), RVec3(0.0_r, Real(10.0f * U), 0.0_r), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING);
	std::printf("box defaults: friction=%g restitution=%g linDamp=%g angDamp=%g maxLinVel=%g maxAngVel=%g convexRadius=%g\n",
		box_settings.mFriction, box_settings.mRestitution, box_settings.mLinearDamping, box_settings.mAngularDamping,
		box_settings.mMaxLinearVelocity, box_settings.mMaxAngularVelocity, static_cast<const BoxShape *>(box_settings.GetShape())->GetConvexRadius());
	BodyID box_id = bi.CreateAndAddBody(box_settings, EActivation::Activate);
	std::printf("box mass = %g kg\n", bi.GetShape(box_id)->GetMassProperties().mMass);

	physics_system.OptimizeBroadPhase();

	// 6) Step: 60 x 1/60 s, 1 collision step per update; then 120 more steps to observe resting contact + sleep
	const float dt = 1.0f / 60.0f;
	const int cNumSteps = 60, cSettleSteps = cm ? 540 : 120; // cm run gets 10 s total to see whether it ever lands / sleeps
	std::vector<double> step_us;
	step_us.reserve(cNumSteps + cSettleSteps);

	// Reference: semi-implicit Euler with Jolt's linear damping v *= (1 - c*dt), in double
	double ref_v = 0.0, ref_y = 10.0 * U;
	float max_speed = 0.0f;
	std::printf("step,  t[s],     y[%s],      vy[%s/s],   refY_symEuler, step_us\n", un, un);
	for (int step = 1; step <= cNumSteps + cSettleSteps; ++step)
	{
		const auto t0 = std::chrono::steady_clock::now();
		const EPhysicsUpdateError err = physics_system.Update(dt, 1, &temp_allocator, &job_system);
		const auto t1 = std::chrono::steady_clock::now();
		const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
		step_us.push_back(us);
		if (err != EPhysicsUpdateError::None)
			std::printf("Update error 0x%x at step %d\n", unsigned(err), step);

		ref_v = (ref_v + dt * -9.81 * U) * (1.0 - 0.05 * dt);
		ref_y += ref_v * dt;

		const RVec3 p = bi.GetCenterOfMassPosition(box_id);
		const Vec3 v = bi.GetLinearVelocity(box_id);
		max_speed = std::max(max_speed, v.Length());
		if (step <= cNumSteps && (step <= 3 || step % 10 == 0))
			std::printf("%4d, %6.3f, %10.6f, %10.6f, %10.6f, %8.1f\n", step, step * dt, double(p.GetY()), double(v.GetY()), ref_y, us);
		if (step == cNumSteps)
			std::printf("== after %d steps: y = %.6f %s (sym-Euler+damping ref %.6f, analytic 10-g t^2/2 = %.6f), max |v| = %.3f %s/s\n",
				cNumSteps, double(p.GetY()), un, ref_y, (10.0 - 0.5 * 9.81) * U, double(max_speed), un);
		if (cm && step > cNumSteps && step % 60 == 0)
			std::printf("   t=%5.2fs y=%10.4f vy=%9.4f active=%d\n", step * dt, double(p.GetY()), double(v.GetY()), bi.IsActive(box_id));
	}

	const RVec3 rest = bi.GetCenterOfMassPosition(box_id);
	std::printf("== after %d steps: y = %.6f %s (resting ideal %.3f, penetration = %.5f %s), max |v| = %.3f %s/s, active = %d\n",
		cNumSteps + cSettleSteps, double(rest.GetY()), un, 0.5 * U, 0.5 * U - double(rest.GetY()), un, double(max_speed), un, bi.IsActive(box_id));

	auto stats = [](std::vector<double> v, const char *label)
	{
		std::sort(v.begin(), v.end());
		double sum = 0; for (double x : v) sum += x;
		std::printf("%s: n=%zu min=%.1f median=%.1f mean=%.1f max=%.1f us\n", label, v.size(), v.front(), v[v.size() / 2], sum / double(v.size()), v.back());
	};
	stats(std::vector<double>(step_us.begin(), step_us.begin() + cNumSteps), "step time (first 60, falling)");
	stats(std::vector<double>(step_us.begin() + cNumSteps, step_us.end()), "step time (remaining, contact/sleep)");

	// 7) Teardown
	bi.RemoveBody(box_id);   bi.DestroyBody(box_id);
	bi.RemoveBody(ground_id); bi.DestroyBody(ground_id);
	UnregisterTypes();
	delete Factory::sInstance;
	Factory::sInstance = nullptr;
	return 0;
}
