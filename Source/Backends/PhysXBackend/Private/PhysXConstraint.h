// PhysXConstraint.h - UE4's PhysXConstraint piece (FConstraintInstance -> PxJoint), re-implemented clean room from the design
// text (02_Architecture 8.2 "FConstraintInstance -> PxD6Joint" [확장 M9]) for the M9 joint contract (docs/Abi.md 9.4).
// PT_BACKEND_NS=PhysX
//
// Recipes (variant keys JointRecipe / JointActorOrder, PhysXSceneConfig.h; every PhysX variant file states them with an
// evidence tag):
//   UE4Compat / -TGS / -Stab  JointRecipe=D6, JointActorOrder=ParentFirst [M]: PxD6JointCreate(SDK, actor2, frame2, actor1,
//                             frame1) - actor0 = Body2 (the parent, or NULL for the world with frame0 = the world pose),
//                             actor1 = Body1 (the child). Linear X / Y / Z eLOCKED (the D6 default, ExtD6Joint.cpp:24 [V]);
//                             ball: eTWIST, eSWING1, eSWING2 eFREE; hinge: eTWIST eFREE, the swings eLOCKED (D6 twist = the
//                             rotation about the frame X axis, PxD6Joint.h:45 [V]; the ABI hinge axis is frame X)
//   SdkDefault                JointRecipe=Native, JointActorOrder=ParentFirst [D]: PxSphericalJointCreate (ball, no limit:
//                             ExtSphericalJoint.cpp:20 [V]) / PxRevoluteJointCreate (hinge about frame X, PxRevoluteJoint.h:59;
//                             no limit or drive, ExtRevoluteJoint.cpp:23 [V]); the actor order of the UE4 row, so the joint type
//                             is the only difference
// PhysX joint frames are actor-space poses (the actor origin, not the COM) - the ABI frames as they are, in native units. Every
// joint is hard (DL31): no limit, drive, break force, softness or inverse mass scale. UE4 enabled joint projection by default
// [M]; PhysX 5.11 has none (removed in 5.2.0, CHANGELOG.md:1703; PxConstraintFlag has no projection bit, PxConstraint.h:28-42
// [V]), so that UE4 behaviour cannot be reproduced and is recorded (effective key joint.projection 0; tag by-design).
// PT_JOINTF_DISABLE_COLLISION: a jointed pair collides only with PxConstraintFlag::eCOLLISION_ENABLED, which is off by default
// (ScConstraintCore.cpp:13 [V]), so the adapter sets it exactly when the ABI flag is clear (UE's default bDisableCollision
// false). Viz runs also set eVISUALIZATION (off by default) so that eJOINT_LOCAL_FRAMES draws the joint.
//
// A joint without a dynamic body (static / kinematic / world only) is accepted and has no effect (docs/Abi.md 9.4 item 6):
// no PhysX joint is created for it (PhysX would reject a constraint between two static actors, NpConstraint.cpp:381-386
// [V]); its snapshot still reports the frames, with zero impulses and PT_JOINTSNAP_SLEEPING (no dynamic body is awake).
//
// Snapshot impulses [V]: PxConstraint::getForce = the solver's writeback impulse x 1 / dt of the last simulate
// (ScConstraintSim.cpp:148-154). The writeback is the impulse on actor0 (sum of the rows' lin0 x applied impulse) and the
// angular impulse on actor0 about the actor1 joint-frame origin (ang -= body0WorldOffset x lin with body0WorldOffset = that
// origin - actor0's COM, DySolverConstraints.cpp:578-603, ExtConstraintHelper.h:134-147); actor1 gets the opposite. The
// adapter reports what Body1 (the child) got, about Frame1World's origin: ParentFirst -(F, T) x dt as they are; ChildFirst
// +(F, T) x dt with T moved from Frame2World's origin to Frame1World's (T1 = T2 + (P2 - P1) x F; the two origins differ by the
// stretch). Units: kg cm/s and kg cm^2/s (x 1 / L, x 1 / L^2). This is the abi-v1.1 convention (the angular impulse about the
// joint anchor). TGS (UE4Compat-TGS, SolverType=TGS): the writeback torque is unusable, so AngularImpulse is NaN with
// PT_JOINTSNAP_ANGULAR_IMPULSE_NA and LinearImpulse stays valid (PhysXConstraint.cpp, MakeJointSnapshot).
#pragma once

#include "PhysXIncludes.h"
#include "PhysXSceneConfig.h"
#include "PhysicsTestAbi/PtBackendKit.h"
#include "PtTypes.h"
#include "PtUnitScale.h"

namespace PhysicsTest::PhysXBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	// One record per joint index (creation order over the scene's CreateConstraints calls, never reused; docs/Abi.md 9.4
	// item 6).
	struct FPhysXJoint
	{
		physx::PxJoint* Joint = nullptr;   // the engine joint; nullptr for a joint without a dynamic body, and once released
		PtTransform Frame1 = {};           // the ABI frames as given (cm): in Body1's actor space ...
		PtTransform Frame2 = {};           // ... and in Body2's (the world pose for the world)
		uint32 Type = PT_JOINT_BALL;
		uint32 Body1 = PT_INDEX_NONE;      // the child (always a body)
		uint32 Body2 = PT_INDEX_NONE;      // the parent, or PT_INDEX_NONE for the world
		uint32 Flags = 0;                  // PT_JOINTF_*
		uint64 CreatedAtStep = 0;          // the scene's step count at creation: no impulse before a step has run the joint
		EPhysXJointRecipe Recipe = EPhysXJointRecipe::D6;
		bool bParentFirst = true;          // actor0 = Body2
		bool bReleased = false;            // a body of the joint was released (ReleaseActors released the joint with it)
	};

	// UE4 FConstraintInstance::CreateJoint_AssumesLocked on PhysX [M] with the variant's recipe (see the file comment). Child =
	// Body1's actor, Parent = Body2's actor or nullptr for the world. nullptr when PhysX refuses the joint.
	physx::PxJoint* CreateJoint_AssumesLocked(physx::PxPhysics& Physics, const FPhysXSceneConfig& Config, const ::PhysicsTest::Abi::FUnitScale& Scale,
		const PtConstraintCreateInfo& Info, physx::PxRigidActor& Child, physx::PxRigidActor* Parent, bool bVisualize);

	// Whether an engine joint holds what the variant asked for: its concrete type (eD6 / eSPHERICAL / eREVOLUTE), the actor
	// order (PxJoint::getActors), the D6 motions (PxD6Joint::getMotion) and eCOLLISION_ENABLED against the ABI flag
	// (PxJoint::getConstraintFlags). Read from the engine (effective key physx.joints).
	bool JointHoldsRecipe(const FPhysXJoint& Record, const physx::PxRigidActor* Body1Actor, const physx::PxRigidActor* Body2Actor);

	// Parent x Local in double (rotation Qp Ql, translation Pp + Qp Pl).
	PtTransform ComposeTransforms(const PtTransform& Parent, const PtTransform& Local);
}
