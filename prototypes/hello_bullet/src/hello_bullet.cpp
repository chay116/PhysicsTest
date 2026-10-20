// hello_bullet.cpp -- PhysicsTest Bullet 3.25 smoke test + mini benchmark.
//  Part 1 (the "hello"): ground + 1 m dynamic box dropped from 10 m, 60 x stepSimulation(1/60, 0).
//  Part 2: Bullet's own "3000 boxes" scene (examples/Benchmarks/BenchmarkDemo.cpp createTest1) run with
//          btDiscreteDynamicsWorld and -- if BT_THREADSAFE -- btDiscreteDynamicsWorldMt on each available
//          task scheduler, printing step timings and a result fingerprint.
#include <btBulletDynamicsCommon.h>
#include <LinearMath/btThreads.h>
#if BT_THREADSAFE
#include <BulletCollision/CollisionDispatch/btCollisionDispatcherMt.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolverMt.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorldMt.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

static double ToMicros(Clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }

struct TimingStats
{
	double total_ms = 0, mean_us = 0, median_us = 0, p95_us = 0, min_us = 0, max_us = 0;
};

static TimingStats Summarize(std::vector<double> us)
{
	TimingStats s;
	if (us.empty()) return s;
	for (double v : us) s.total_ms += v / 1000.0;
	s.mean_us = s.total_ms * 1000.0 / double(us.size());
	std::sort(us.begin(), us.end());
	s.min_us = us.front();
	s.max_us = us.back();
	s.median_us = us[us.size() / 2];
	s.p95_us = us[std::min(us.size() - 1, size_t(double(us.size()) * 0.95))];
	return s;
}

static const char* ActivationName(int state)
{
	switch (state)
	{
		case ACTIVE_TAG: return "ACTIVE_TAG";
		case ISLAND_SLEEPING: return "ISLAND_SLEEPING";
		case WANTS_DEACTIVATION: return "WANTS_DEACTIVATION";
		case DISABLE_DEACTIVATION: return "DISABLE_DEACTIVATION";
		case DISABLE_SIMULATION: return "DISABLE_SIMULATION";
		default: return "?";
	}
}

// --------------------------------------------------------------------------------------------
// Part 1: hello drop
// --------------------------------------------------------------------------------------------
static int RunHelloDrop()
{
	std::printf("=== Part 1: hello drop (btDiscreteDynamicsWorld) ===\n");
	btDefaultCollisionConfiguration config;
	btCollisionDispatcher dispatcher(&config);
	btDbvtBroadphase broadphase;
	btSequentialImpulseConstraintSolver solver;
	btDiscreteDynamicsWorld world(&dispatcher, &broadphase, &solver, &config);
	const btScalar g = btScalar(9.81);
	world.setGravity(btVector3(0, -g, 0));  // Bullet default is (0,-10,0)

	// Ground: static 100 x 2 x 100 m box whose top face is y = 0.
	btBoxShape groundShape(btVector3(50, 1, 50));
	btRigidBody::btRigidBodyConstructionInfo groundCI(0, nullptr, &groundShape);
	groundCI.m_startWorldTransform.setOrigin(btVector3(0, -1, 0));
	btRigidBody ground(groundCI);
	world.addRigidBody(&ground);

	// Dynamic 1 m cube, 1 kg, centre at y = 10 m.
	btBoxShape boxShape(btVector3(0.5f, 0.5f, 0.5f));
	const btScalar mass = 1;
	btVector3 inertia(0, 0, 0);
	boxShape.calculateLocalInertia(mass, inertia);
	btRigidBody::btRigidBodyConstructionInfo boxCI(mass, nullptr, &boxShape, inertia);
	boxCI.m_startWorldTransform.setOrigin(btVector3(0, 10, 0));
	btRigidBody box(boxCI);
	world.addRigidBody(&box);

	std::printf("box margin=%.4f (CONVEX_DISTANCE_MARGIN=0.04, setSafeMargin -> min(0.04, 0.1*minHalfExtent))\n",
				double(boxShape.getMargin()));
	std::printf("solverInfo: numIterations=%d erp=%.2f erp2=%.2f splitImpulse=%d splitThr=%.3f solverMode=0x%x\n",
				world.getSolverInfo().m_numIterations, double(world.getSolverInfo().m_erp),
				double(world.getSolverInfo().m_erp2), world.getSolverInfo().m_splitImpulse,
				double(world.getSolverInfo().m_splitImpulsePenetrationThreshold), world.getSolverInfo().m_solverMode);

	const btScalar dt = btScalar(1.0 / 60.0);
	std::vector<double> stepUs;
	std::printf("%5s %12s %12s %10s\n", "step", "y[m]", "vy[m/s]", "step[us]");
	for (int i = 1; i <= 60; ++i)
	{
		const auto t0 = Clock::now();
		world.stepSimulation(dt, 0);  // maxSubSteps=0 -> exactly one internal step of dt
		const auto t1 = Clock::now();
		stepUs.push_back(ToMicros(t1 - t0));
		const btVector3& p = box.getWorldTransform().getOrigin();
		std::printf("%5d %12.6f %12.6f %10.2f\n", i, double(p.y()), double(box.getLinearVelocity().y()), stepUs.back());
	}
	const btVector3 p60 = box.getWorldTransform().getOrigin();
	const double n = 60, h = 1.0 / 60.0;
	const double semiImplicit = 10.0 - double(g) * h * h * n * (n + 1) / 2.0;  // v += g dt; x += v dt
	const double exact = 10.0 - 0.5 * double(g) * 1.0;
	const TimingStats s = Summarize(stepUs);
	std::printf("final position after 60 steps: (%.6f, %.6f, %.6f)\n", double(p60.x()), double(p60.y()), double(p60.z()));
	std::printf("  expected y semi-implicit Euler = %.6f, analytic 0.5gt^2 = %.6f  (diff to SIE %.2e)\n", semiImplicit, exact,
				double(p60.y()) - semiImplicit);
	std::printf("  step time: total %.3f ms, mean %.2f us, median %.2f us, min %.2f us, max %.2f us\n", s.total_ms,
				s.mean_us, s.median_us, s.min_us, s.max_us);

	// Keep going to see landing / resting / sleeping behaviour (result-comparison metrics).
	int sleepStep = -1;
	for (int i = 61; i <= 600; ++i)
	{
		world.stepSimulation(dt, 0);
		if (sleepStep < 0 && box.getActivationState() == ISLAND_SLEEPING) sleepStep = i;
	}
	const btVector3 pEnd = box.getWorldTransform().getOrigin();
	std::printf("after 600 steps: y=%.6f (rest height 0.5 => penetration %.4f m), state=%s, first ISLAND_SLEEPING at step %d (%.3f s)\n",
				double(pEnd.y()), 0.5 - double(pEnd.y()), ActivationName(box.getActivationState()), sleepStep,
				sleepStep * h);

	world.removeRigidBody(&box);
	world.removeRigidBody(&ground);
	return 0;
}

// --------------------------------------------------------------------------------------------
// Part 2: "3000 boxes" (BenchmarkDemo createTest1) -- ST vs MT world
// --------------------------------------------------------------------------------------------
struct Scene
{
	std::unique_ptr<btDefaultCollisionConfiguration> config;
	std::unique_ptr<btCollisionDispatcher> dispatcher;
	std::unique_ptr<btBroadphaseInterface> broadphase;
	std::unique_ptr<btConstraintSolver> solver;    // ST solver, or btConstraintSolverPoolMt
	std::unique_ptr<btConstraintSolver> solverMt;  // btSequentialImpulseConstraintSolverMt (MT only)
	std::unique_ptr<btDiscreteDynamicsWorld> world;
	std::vector<std::unique_ptr<btCollisionShape>> shapes;
	std::vector<std::unique_ptr<btDefaultMotionState>> motionStates;
	std::vector<std::unique_ptr<btRigidBody>> bodies;

	~Scene()
	{
		if (world)
			for (auto& b : bodies) world->removeRigidBody(b.get());
		bodies.clear();
		world.reset();  // world before solver/dispatcher/broadphase/config
		solverMt.reset();
		solver.reset();
		broadphase.reset();
		dispatcher.reset();
		config.reset();
	}

	btRigidBody* AddBody(btScalar mass, const btVector3& pos, btCollisionShape* shape)
	{
		btVector3 inertia(0, 0, 0);
		if (mass != 0) shape->calculateLocalInertia(mass, inertia);
		btTransform t;
		t.setIdentity();
		t.setOrigin(pos);
		motionStates.push_back(std::make_unique<btDefaultMotionState>(t));
		btRigidBody::btRigidBodyConstructionInfo ci(mass, motionStates.back().get(), shape, inertia);
		bodies.push_back(std::make_unique<btRigidBody>(ci));
		world->addRigidBody(bodies.back().get());
		return bodies.back().get();
	}
};

#if BT_THREADSAFE
// btCollisionDispatcherMt sizes its per-thread manifold arrays with btGetTaskScheduler()->getNumThreads()
// (btCollisionDispatcherMt.cpp:31-32) but indexes them with btGetCurrentThreadIndex() (:73, :94).
// A scheduler whose worker indices can reach getNumThreads() (PPL: the calling thread is index 0 *plus*
// N ConcRT workers) writes out of bounds. This proxy is installed only while the dispatcher is constructed
// so the arrays get BT_MAX_THREAD_COUNT slots.
struct SizingProxyScheduler : public btITaskScheduler
{
	SizingProxyScheduler() : btITaskScheduler("SizingProxy") {}
	int getMaxNumThreads() const override { return int(BT_MAX_THREAD_COUNT); }
	int getNumThreads() const override { return int(BT_MAX_THREAD_COUNT); }
	void setNumThreads(int) override {}
	void parallelFor(int b, int e, int, const btIParallelForBody& body) override { body.forLoop(b, e); }
	btScalar parallelSum(int b, int e, int, const btIParallelSumBody& body) override { return body.sumLoop(b, e); }
};
#endif
static bool g_dispatcherHeadroom = false;

static void CreateWorld(Scene& sc, bool mt)
{
	// BenchmarkDemo / MultiThreadedDemo use 80000-entry pools; default is 4096 (btDefaultCollisionConfiguration.h:35-36).
	btDefaultCollisionConstructionInfo cci;
	cci.m_defaultMaxPersistentManifoldPoolSize = 80000;
	cci.m_defaultMaxCollisionAlgorithmPoolSize = 80000;
	sc.config = std::make_unique<btDefaultCollisionConfiguration>(cci);
	sc.broadphase = std::make_unique<btDbvtBroadphase>();
	if (!mt)
	{
		sc.dispatcher = std::make_unique<btCollisionDispatcher>(sc.config.get());
		sc.solver = std::make_unique<btSequentialImpulseConstraintSolver>();
		sc.world = std::make_unique<btDiscreteDynamicsWorld>(sc.dispatcher.get(), sc.broadphase.get(), sc.solver.get(),
															 sc.config.get());
	}
	else
	{
#if BT_THREADSAFE
		if (g_dispatcherHeadroom)
		{
			static SizingProxyScheduler proxy;
			btITaskScheduler* real = btGetTaskScheduler();
			btSetTaskScheduler(&proxy);
			sc.dispatcher = std::make_unique<btCollisionDispatcherMt>(sc.config.get(), 40);
			btSetTaskScheduler(real);
		}
		else
		{
			sc.dispatcher = std::make_unique<btCollisionDispatcherMt>(sc.config.get(), 40);
		}
		btConstraintSolver* solvers[BT_MAX_THREAD_COUNT];
		for (int i = 0; i < int(BT_MAX_THREAD_COUNT); ++i) solvers[i] = new btSequentialImpulseConstraintSolver();
		auto* pool = new btConstraintSolverPoolMt(solvers, BT_MAX_THREAD_COUNT);  // pool owns & deletes the solvers
		auto* solverMt = new btSequentialImpulseConstraintSolverMt();
		sc.solver.reset(pool);
		sc.solverMt.reset(solverMt);
		sc.world = std::make_unique<btDiscreteDynamicsWorldMt>(sc.dispatcher.get(), sc.broadphase.get(), pool, solverMt,
															   sc.config.get());
#endif
	}
	// BenchmarkDemo::initPhysics settings
	sc.world->getSolverInfo().m_solverMode |= SOLVER_ENABLE_FRICTION_DIRECTION_CACHING;
	sc.world->getSolverInfo().m_numIterations = 5;
	sc.world->setGravity(btVector3(0, -10, 0));
}

static void Build3000Boxes(Scene& sc)
{
	auto ground = std::make_unique<btBoxShape>(btVector3(250, 50, 250));
	sc.AddBody(0, btVector3(0, -50, 0), ground.get());
	sc.shapes.push_back(std::move(ground));

	const int size = 8;
	const float cubeSize = 1.0f, spacing = cubeSize;
	auto block = std::make_unique<btBoxShape>(btVector3(cubeSize, cubeSize, cubeSize));
	btVector3 pos(0.0f, cubeSize * 2, 0.0f);
	float offset = -size * (cubeSize * 2.0f + spacing) * 0.5f;
	for (int k = 0; k < 47; k++)
	{
		for (int j = 0; j < size; j++)
		{
			pos[2] = offset + float(j) * (cubeSize * 2.0f + spacing);
			for (int i = 0; i < size; i++)
			{
				pos[0] = offset + float(i) * (cubeSize * 2.0f + spacing);
				sc.AddBody(2.0f, pos, block.get());
			}
		}
		offset -= 0.05f * spacing * (size - 1);
		pos[1] += (cubeSize * 2.0f + spacing);
	}
	sc.shapes.push_back(std::move(block));
}

static void RunBoxes(const char* label, bool mt, int steps)
{
	Scene sc;
	CreateWorld(sc, mt);
	Build3000Boxes(sc);
	const int dynamicCount = int(sc.bodies.size()) - 1;
	std::vector<double> us;
	us.reserve(steps);
	const auto wall0 = Clock::now();
	for (int i = 0; i < steps; ++i)
	{
		const auto t0 = Clock::now();
		sc.world->stepSimulation(btScalar(1.0 / 60.0), 0);
		us.push_back(ToMicros(Clock::now() - t0));
	}
	const double wallMs = ToMicros(Clock::now() - wall0) / 1000.0;

	// result fingerprint
	int sleeping = 0, fellThrough = 0;
	double sumY = 0, maxY = -1e30, sumX = 0, sumZ = 0;
	for (size_t b = 1; b < sc.bodies.size(); ++b)
	{
		const btRigidBody* rb = sc.bodies[b].get();
		const btVector3& p = rb->getWorldTransform().getOrigin();
		sumX += p.x();
		sumY += p.y();
		sumZ += p.z();
		maxY = std::max(maxY, double(p.y()));
		if (rb->getActivationState() == ISLAND_SLEEPING) ++sleeping;
		if (p.y() < 0.5) ++fellThrough;
	}
	int contacts = 0;
	const int manifolds = sc.dispatcher->getNumManifolds();
	for (int m = 0; m < manifolds; ++m) contacts += sc.dispatcher->getManifoldByIndexInternal(m)->getNumContacts();

	auto window = [&](int a, int b) {
		return Summarize(std::vector<double>(us.begin() + a, us.begin() + std::min(b, int(us.size()))));
	};
	const TimingStats all = Summarize(us), w0 = window(0, 60), w1 = window(60, 180), w2 = window(180, steps);
	std::printf("[%-22s] bodies=%d steps=%d wall=%.1f ms | per-step mean %.3f ms, median %.3f, p95 %.3f, max %.3f\n", label,
				dynamicCount, steps, wallMs, all.mean_us / 1000, all.median_us / 1000, all.p95_us / 1000,
				all.max_us / 1000);
	std::printf("    mean by window: steps 1-60 %.3f ms | 61-180 %.3f ms | 181-%d %.3f ms\n", w0.mean_us / 1000,
				w1.mean_us / 1000, steps, w2.mean_us / 1000);
	std::printf("    result: avgPos=(%.4f, %.4f, %.4f) maxY=%.3f sleeping=%d/%d belowGround=%d manifolds=%d contacts=%d\n",
				sumX / dynamicCount, sumY / dynamicCount, sumZ / dynamicCount, maxY, sleeping, dynamicCount, fellThrough,
				manifolds, contacts);
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);  // keep output if something crashes mid-run
	const int steps = (argc > 1) ? std::max(181, std::atoi(argv[1])) : 600;
	const int only = (argc > 2) ? std::atoi(argv[2]) : -1;  // -1 = all runs; 0 = ST; 1.. = MT scheduler index
	const int forcedThreads = (argc > 3) ? std::atoi(argv[3]) : 0;  // 0 = scheduler max
	const int headroomArg = (argc > 4) ? std::atoi(argv[4]) : -1;   // -1 = auto (PPL only), 0 = off, 1 = on
	(void)forcedThreads;  // only used when BT_THREADSAFE
	(void)headroomArg;
	std::printf("Bullet version %d, sizeof(btScalar)=%zu, BT_THREADSAFE=%d", btGetVersion(), sizeof(btScalar),
#if BT_THREADSAFE
				1
#else
				0
#endif
	);
#ifdef BT_USE_SSE
	std::printf(", BT_USE_SSE");
#endif
#ifdef BT_USE_SIMD_VECTOR3
	std::printf(", BT_USE_SIMD_VECTOR3");
#endif
	std::printf("\n");

	RunHelloDrop();

	std::printf("\n=== Part 2: BenchmarkDemo '3000 boxes' (8x8x47, half-extent 1, mass 2, 5 iterations) ===\n");
	if (only < 0 || only == 0) RunBoxes("ST btDiscreteDynamicsWorld", false, steps);
#if BT_THREADSAFE
	struct Sched
	{
		const char* label;
		btITaskScheduler* ts;
		bool owned;
		bool headroom;
	};
	std::vector<Sched> scheds;
	scheds.push_back({"MT world + sequential", btGetSequentialTaskScheduler(), false, false});
	if (btITaskScheduler* ts = btCreateDefaultTaskScheduler()) scheds.push_back({"MT world + Win32 default", ts, true, false});
	if (btITaskScheduler* ts = btGetPPLTaskScheduler()) scheds.push_back({"MT world + PPL", ts, false, true});
	if (btITaskScheduler* ts = btGetOpenMPTaskScheduler()) scheds.push_back({"MT world + OpenMP", ts, false, false});
	for (size_t si = 0; si < scheds.size(); ++si)
	{
		Sched& s = scheds[si];
		if (only >= 0 && only != int(si) + 1) continue;
		const int before = s.ts->getNumThreads();
		if (forcedThreads > 0)
			s.ts->setNumThreads(forcedThreads);
		else if (s.ts->getNumThreads() <= 1)
			s.ts->setNumThreads(s.ts->getMaxNumThreads());  // as MultiThreadedDemo does
		btSetTaskScheduler(s.ts);
		g_dispatcherHeadroom = (headroomArg < 0) ? s.headroom : (headroomArg != 0);
		std::printf("scheduler %-10s threads %d -> %d (max %d) dispatcherHeadroom=%d\n", s.ts->getName(), before,
					s.ts->getNumThreads(), s.ts->getMaxNumThreads(), int(g_dispatcherHeadroom));
		RunBoxes(s.label, true, steps);
	}
	btSetTaskScheduler(btGetSequentialTaskScheduler());
	for (auto& s : scheds)
		if (s.owned) delete s.ts;
#endif
	return 0;
}
