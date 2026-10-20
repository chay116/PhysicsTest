// PhysX 5.11 CPU-only hello test: foundation/physics/scene, ground plane, one dynamic box from 10 m,
// 60 steps of 1/60 s, per-step timing. Then a settle phase to prove contact generation works.
#include "PxPhysicsAPI.h"
#include <chrono>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cstdlib>

using namespace physx;

static PxDefaultAllocator     gAllocator;
static PxDefaultErrorCallback gErrorCallback;

int main()
{
    std::printf("PhysX SDK %d.%d.%d | PX_SUPPORT_GPU_PHYSX=%d PX_DEBUG=%d PX_CHECKED=%d PX_PROFILE=%d"
#ifdef PX_PHYSX_STATIC_LIB
                " PX_PHYSX_STATIC_LIB"
#endif
#ifdef NDEBUG
                " NDEBUG"
#endif
#ifdef _DEBUG
                " _DEBUG"
#endif
                "\n",
                PX_PHYSICS_VERSION_MAJOR, PX_PHYSICS_VERSION_MINOR, PX_PHYSICS_VERSION_BUGFIX,
                PX_SUPPORT_GPU_PHYSX, PX_DEBUG, PX_CHECKED, PX_PROFILE);

    PxFoundation* foundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
    if (!foundation) { std::printf("PxCreateFoundation failed\n"); return 1; }

    PxTolerancesScale scale; // length=1 (m), speed=10 (m/s)
    PxPhysics* physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, scale, /*trackOutstandingAllocations*/ false, nullptr);
    if (!physics) { std::printf("PxCreatePhysics failed\n"); return 1; }

    const char* thrEnv = std::getenv("PX_THREADS");
    const PxU32 numThreads = thrEnv ? PxU32(std::atoi(thrEnv)) : 2u;  // default per task spec: 2 worker threads
    PxDefaultCpuDispatcher* dispatcher = PxDefaultCpuDispatcherCreate(numThreads);
    std::printf("CPU dispatcher worker threads=%u (eWAIT_FOR_WORK)\n", numThreads);

    PxSceneDesc sceneDesc(physics->getTolerancesScale());
    sceneDesc.gravity       = PxVec3(0.0f, -9.81f, 0.0f);
    sceneDesc.cpuDispatcher = dispatcher;
    sceneDesc.filterShader  = PxDefaultSimulationFilterShader;
    PxScene* scene = physics->createScene(sceneDesc);
    if (!scene) { std::printf("createScene failed\n"); return 1; }

    std::printf("SceneDesc defaults: solverType=%s broadPhase=%d frictionType(n/a in 5.x) bounceThreshold=%.3f "
                "frictionOffsetThreshold=%.3f\n",
                sceneDesc.solverType == PxSolverType::ePGS ? "PGS" : "TGS",
                int(sceneDesc.broadPhaseType), sceneDesc.bounceThresholdVelocity, sceneDesc.frictionOffsetThreshold);

    PxMaterial* material = physics->createMaterial(0.5f, 0.5f, 0.0f); // static, dynamic friction, restitution

    PxRigidStatic* ground = PxCreatePlane(*physics, PxPlane(0, 1, 0, 0), *material);
    scene->addActor(*ground);

    const PxReal halfExtent = 0.5f;
    PxRigidDynamic* box = PxCreateDynamic(*physics, PxTransform(PxVec3(0.0f, 10.0f, 0.0f)),
                                          PxBoxGeometry(halfExtent, halfExtent, halfExtent), *material, /*density*/ 1000.0f);
    scene->addActor(*box);
    std::printf("Box mass=%.3f kg, solverIters pos=%u vel=%u, linDamp=%.3f angDamp=%.3f sleepThr=%.4f\n",
                box->getMass(), 
                [&]{ PxU32 p, v; box->getSolverIterationCounts(p, v); return p; }(),
                [&]{ PxU32 p, v; box->getSolverIterationCounts(p, v); return v; }(),
                box->getLinearDamping(), box->getAngularDamping(), box->getSleepThreshold());

    const PxReal dt = 1.0f / 60.0f;
    const int steps = 60;
    std::vector<double> us(steps);
    for (int i = 0; i < steps; ++i)
    {
        auto t0 = std::chrono::steady_clock::now();
        scene->simulate(dt);
        scene->fetchResults(true);
        auto t1 = std::chrono::steady_clock::now();
        us[i] = std::chrono::duration<double, std::micro>(t1 - t0).count();
        const PxVec3 p = box->getGlobalPose().p;
        const PxVec3 v = box->getLinearVelocity();
        std::printf("step %2d  t=%.4f  y=%.6f  vy=%.6f  %.1f us\n", i + 1, (i + 1) * dt, p.y, v.y, us[i]);
    }
    const PxVec3 p60 = box->getGlobalPose().p;
    // Symplectic Euler: v_n = -g*n*dt, y_n = y0 - g*dt^2*n(n+1)/2
    const double analytic = 10.0 - 9.81 * double(dt) * double(dt) * (steps * (steps + 1) / 2.0);
    std::vector<double> s = us; std::sort(s.begin(), s.end());
    const double total = std::accumulate(us.begin(), us.end(), 0.0);
    std::printf("FINAL after %d steps: pos=(%.6f, %.6f, %.6f)  analytic symplectic-Euler y=%.6f  closed-form y=%.6f\n",
                steps, p60.x, p60.y, p60.z, analytic, 10.0 - 0.5 * 9.81 * 1.0);
    std::printf("TIMING (simulate+fetchResults): total=%.1f us mean=%.2f us median=%.2f us min=%.2f us max=%.2f us (first step=%.1f us)\n",
                total, total / steps, s[steps / 2], s.front(), s.back(), us[0]);

    // Settle phase: 240 more steps (4 s) so the box lands and rests on the plane (expect y ~= 0.5).
    int sleptAt = -1;
    for (int i = 0; i < 240; ++i)
    {
        scene->simulate(dt);
        scene->fetchResults(true);
        if (sleptAt < 0 && box->isSleeping()) sleptAt = steps + i + 1;
    }
    const PxVec3 pr = box->getGlobalPose().p;
    std::printf("SETTLE after %d total steps: pos=(%.6f, %.6f, %.6f) sleeping=%d (first asleep at step %d)\n",
                steps + 240, pr.x, pr.y, pr.z, int(box->isSleeping()), sleptAt);

    scene->release();
    dispatcher->release();
    physics->release();
    foundation->release();
    std::printf("hello_physx done.\n");
    return 0;
}
