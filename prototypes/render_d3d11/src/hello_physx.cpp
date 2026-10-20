// Minimal PhysX 5 smoke test (CPU dispatcher, static libs)
#include <PxPhysicsAPI.h>
#include <cstdio>
using namespace physx;
#ifdef PX_PHYSX_STATIC_LIB
static const int kStatic = 1;
#else
static const int kStatic = 0;
#endif
static PxDefaultAllocator gAlloc; static PxDefaultErrorCallback gErr;
int main() {
  PxFoundation* f = PxCreateFoundation(PX_PHYSICS_VERSION, gAlloc, gErr);
  PxPhysics* p = PxCreatePhysics(PX_PHYSICS_VERSION, *f, PxTolerancesScale());
  PxDefaultCpuDispatcher* disp = PxDefaultCpuDispatcherCreate(4);
  PxSceneDesc sd(p->getTolerancesScale()); sd.gravity = PxVec3(0, 0, -9.81f); sd.cpuDispatcher = disp; sd.filterShader = PxDefaultSimulationFilterShader;
  PxScene* s = p->createScene(sd);
  PxMaterial* m = p->createMaterial(0.5f, 0.5f, 0.0f);
  PxRigidStatic* ground = PxCreateStatic(*p, PxTransform(PxVec3(0, 0, -1)), PxBoxGeometry(50, 50, 1), *m); s->addActor(*ground);
  PxRigidDynamic* box = PxCreateDynamic(*p, PxTransform(PxVec3(0, 0, 10)), PxBoxGeometry(0.5f, 0.5f, 0.5f), *m, 1.0f); s->addActor(*box);
  for (int i = 0; i < 120; ++i) { s->simulate(1.0f / 60.0f); s->fetchResults(true); }
  std::printf("[physx] after 2s: z=%.4f  (PX_PHYSX_STATIC_LIB defined=%d)\n", double(box->getGlobalPose().p.z), kStatic);
  s->release(); disp->release(); p->release(); f->release();
}


