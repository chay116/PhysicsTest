// Minimal Bullet smoke test
#include <btBulletDynamicsCommon.h>
#include <cstdio>
int main() {
  btDefaultCollisionConfiguration cfg; btCollisionDispatcher disp(&cfg); btDbvtBroadphase bp; btSequentialImpulseConstraintSolver solver;
  btDiscreteDynamicsWorld world(&disp, &bp, &solver, &cfg); world.setGravity(btVector3(0, 0, -9.81f));
  btBoxShape groundShape(btVector3(50, 50, 1)); btDefaultMotionState gms(btTransform(btQuaternion::getIdentity(), btVector3(0, 0, -1)));
  btRigidBody ground(btRigidBody::btRigidBodyConstructionInfo(0, &gms, &groundShape)); world.addRigidBody(&ground);
  btBoxShape boxShape(btVector3(0.5f, 0.5f, 0.5f)); btVector3 inertia; boxShape.calculateLocalInertia(1, inertia);
  btDefaultMotionState bms(btTransform(btQuaternion::getIdentity(), btVector3(0, 0, 10)));
  btRigidBody box(btRigidBody::btRigidBodyConstructionInfo(1, &bms, &boxShape, inertia)); world.addRigidBody(&box);
  for (int i = 0; i < 120; ++i) world.stepSimulation(1.0f / 60.0f, 0);
  std::printf("[bullet] after 2s: z=%.4f  (BT_USE_SSE: %s)\n", double(box.getCenterOfMassPosition().z()),
#ifdef BT_USE_SSE
    "yes"
#else
    "no"
#endif
  );
  world.removeRigidBody(&box); world.removeRigidBody(&ground);
}
