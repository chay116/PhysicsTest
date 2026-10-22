// PhysScene_BulletJoints.cpp - the joints of FPhysScene_Bullet (M9, docs/Abi.md 9.4; the PhysScene_Bullet.h header comment
// "Joints"): CreateConstraints, their release with a body, and the joint snapshot. [확장 M9]
//
// Mapping (docs/Abi.md 9.4 "엔진별 매핑", Bullet row):
//   PT_JOINT_BALL   btPoint2PointConstraint(rbA, rbB, pivotInA, pivotInB): the frame origins in each body's frame
//                   (btPoint2PointConstraint.h:75 [V]); rotation free.
//   PT_JOINT_HINGE  btHingeConstraint(rbA, rbB, frameInA, frameInB, useReferenceFrameA = false) (btHingeConstraint.h:104 [V]):
//                   the frame origins coincide and the two frame Z axes stay aligned (btHingeConstraint.cpp:251 getColumn(2)
//                   [V]); no limit (btAngularLimit's half range -1 = free, btTypedConstraint.h:457-466 [V]) and no motor.
//                   The ABI's twist axis is frame X, so both frames get the extra rotation GBulletHingeAxisFromX.
//   rbA = Body1 (the child): btJointFeedback's "body A" is then the body the ABI reports (LinearImpulse / AngularImpulse on
//   Body1). rbB = Body2, or the scene's world anchor (a static rigid body at the origin that is never added to the world: the
//   solver gives every static body its fixed solver body, btSequentialImpulseConstraintSolver::getOrInitSolverBody [V]), so a
//   world joint's Frame2 is its world pose as the ABI gives it. Bullet's single-body constructors would instead derive the
//   world end from the child's current pose.
//   Pivots and frames are in the rigid body's frame = the COM principal frame of a dynamic body (actor x ComLocal), the actor
//   frame of a static or kinematic one (BulletMath.h, M5 B-4): frame in body = ComLocalInverse x frame in actor space. A
//   compound body (several elements or a COM offset) is no different: its rigid-body frame is the same COM frame.
//   Solver: the world's btSequentialImpulseConstraintSolver, its iterations (NumIterations) and btContactSolverInfo::m_erp
//   (Bullet's default 0.2, Baumgarte, no split impulse for joints), CFM 0; joints are solved with the contacts of their island
//   (calculateSimulationIslands unites the islands of two dynamic jointed bodies [V], so a chain sleeps as a whole).
// M9 finding J-1 (engine behaviour, not tuned away): btPoint2PointConstraint is three scalar rows along the world axes, each with
// the angular term (a x e)^T I^-1 (a x e) of the pivot lever a. For M6a's ball pendulum (L 100 cm, a solid 5 cm 1 kg bob: m L^2 / I
// = 1000) that term dominates, the rows couple through the bob's spin and 10 Gauss-Seidel iterations leave 2.3 cm of stretch (the
// first step spins the bob at 0.55 rad/s instead of 0.014); 50 iterations 0.025 cm. The hinge (btHingeConstraint's frame-offset
// rows, HINGE_USE_FRAME_OFFSET) and btGeneric6DofSpring2Constraint with the same freedoms stay at 0.005 cm with 10 there - but the
// latter is no general cure: on a light sphere carrying a cube (PhysicsTest.BulletBackend.JointFramesAndComOffsets) it stretches
// 45 cm where the point constraint stretches 4.5. The scene option BallJoint=Generic6DofSpring2 selects it for comparison only;
// the default is the docs/Abi.md 9.4 mapping.
#include "PhysScene_Bullet.h"

#include "BulletMath.h"

namespace PhysicsTest::BulletBackend
{
	namespace Abi = ::PhysicsTest::Abi;

	btRigidBody& FPhysScene_Bullet::GetWorldAnchor()
	{
		if (WorldAnchor == nullptr)
		{
			// As btTypedConstraint::getFixedBody (btTypedConstraint.cpp [V]) but owned by this scene: mass 0 (CF_STATIC_OBJECT),
			// no shape, identity pose. Never added to the world, so it takes part in no broad phase, island or sleep test.
			WorldAnchor = new btRigidBody(btScalar(0), nullptr, nullptr, btVector3(0, 0, 0));
			WorldAnchor->setUserIndex(-1);
		}
		return *WorldAnchor;
	}

	PtResult FPhysScene_Bullet::CreateConstraints(const Abi::TStridedView<PtConstraintCreateInfo> Infos)
	{
		// The kit has checked every joint (type, bodies, flags, Reserved, unit frames, the capability) and numbers joint i of this
		// call GetNumJoints() + i; this scene keeps one record per index.
		const uint32 Num = Infos.Num();
		if (static_cast<uint32>(Joints.size()) != GetNumJoints())
		{
			return GetKit().SetLastError(PT_E_INTERNAL, "Bullet: %d joint records for %u joints the kit counted", Joints.size(), GetNumJoints());
		}
		// All or nothing: every Bullet constraint is built before the world sees any of them (nothing below fails after that).
		btAlignedObjectArray<FBulletJoint> Created;
		Created.reserve(static_cast<int>(Num));
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			const PtConstraintCreateInfo& Info = Infos[Index];
			FBulletJoint Joint;
			Joint.Type = Info.Type;
			Joint.Body1 = Info.Body1;
			Joint.Body2 = Info.Body2;
			Joint.Flags = Info.Flags;
			Joint.Frame1 = Info.Frame1;
			Joint.Frame2 = Info.Frame2;
			const FBulletBody& Record1 = Bodies[static_cast<int32>(Info.Body1)];
			const FBulletBody* Record2 = Info.Body2 != PT_INDEX_NONE ? &Bodies[static_cast<int32>(Info.Body2)] : nullptr;
			const bool bAnyDynamic = Record1.Motion == PT_MOTION_DYNAMIC || (Record2 != nullptr && Record2->Motion == PT_MOTION_DYNAMIC);
			if (bAnyDynamic)
			{
				btRigidBody& RigidA = *Record1.Body;
				btRigidBody& RigidB = Record2 != nullptr ? *Record2->Body : GetWorldAnchor();
				// Each frame in its rigid body's frame (the world anchor's frame is the world).
				const PtTransform FrameInA = TransformMultiply(Record1.ComLocalInverse, Info.Frame1);
				const PtTransform FrameInB = Record2 != nullptr ? TransformMultiply(Record2->ComLocalInverse, Info.Frame2) : Info.Frame2;
				if (Info.Type == PT_JOINT_BALL && Options.BallJoint == EBallJoint::Generic6DofSpring2)
				{
					// BallJoint=Generic6DofSpring2 (comparison only): Bullet's 6-DOF joint with the three linear axes locked (lower =
					// upper = 0) and the three angular axes free (lower > upper), btGeneric6DofSpring2Constraint.h [V] - the shape of UE's
					// ConfigureAsBallAndSocket on a D6 joint.
					btGeneric6DofSpring2Constraint* Ball = new btGeneric6DofSpring2Constraint(RigidA, RigidB, ToBtTransform(FrameInA), ToBtTransform(FrameInB));
					Ball->setLinearLowerLimit(btVector3(0, 0, 0));
					Ball->setLinearUpperLimit(btVector3(0, 0, 0));
					Ball->setAngularLowerLimit(btVector3(1, 1, 1));
					Ball->setAngularUpperLimit(btVector3(-1, -1, -1));
					Joint.Constraint = Ball;
				}
				else if (Info.Type == PT_JOINT_BALL)
				{
					Joint.Constraint = new btPoint2PointConstraint(RigidA, RigidB, ToBtLength(FrameInA.Translation), ToBtLength(FrameInB.Translation));
				}
				else
				{
					const PtTransform HingeA = TransformMultiply(FrameInA, PtTransform{ GBulletHingeAxisFromX, PtVec3{} });
					const PtTransform HingeB = TransformMultiply(FrameInB, PtTransform{ GBulletHingeAxisFromX, PtVec3{} });
					Joint.Constraint = new btHingeConstraint(RigidA, RigidB, ToBtTransform(HingeA), ToBtTransform(HingeB), false);
				}
				Joint.PivotInA = ToBtLength(FrameInA.Translation);
				Joint.LeverAtSolve = RigidA.getCenterOfMassTransform().getBasis() * Joint.PivotInA;
				Joint.Feedback = new btJointFeedback();
				Joint.Feedback->m_appliedForceBodyA.setZero();
				Joint.Feedback->m_appliedTorqueBodyA.setZero();
				Joint.Feedback->m_appliedForceBodyB.setZero();
				Joint.Feedback->m_appliedTorqueBodyB.setZero();
				Joint.Constraint->setJointFeedback(Joint.Feedback);
				Joint.Constraint->setUserConstraintId(static_cast<int>(GetNumJoints() + Index));
			}
			Created.push_back(Joint);
		}

		for (int32 Slot = 0; Slot < Created.size(); ++Slot)
		{
			FBulletJoint& Joint = Created[Slot];
			if (Joint.Constraint != nullptr)
			{
				// PT_JOINTF_DISABLE_COLLISION: btRigidBody::addConstraintRef makes each body ignore the other (setIgnoreCollisionCheck,
				// both directions; btCollisionDispatcher::needsCollision tests both [V]). No effect for a world joint (the anchor has
				// no proxy), so it is not asked for there.
				const bool bDisableCollision = (Joint.Flags & PT_JOINTF_DISABLE_COLLISION) != 0 && Joint.Body2 != PT_INDEX_NONE;
				World->addConstraint(Joint.Constraint, bDisableCollision);
				if (bDisableCollision)
				{
					// A pair that already has a manifold (a joint made after the bodies stepped) would keep solving its stale points:
					// the island manager collects every manifold whose bodies need a response, the ignore list only stops the narrow
					// phase (btSimulationIslandManager::buildAndProcessIslands, btCollisionDispatcher::defaultNearCallback [V]).
					btBroadphaseProxy* Proxy1 = Bodies[static_cast<int32>(Joint.Body1)].Body->getBroadphaseHandle();
					btBroadphaseProxy* Proxy2 = Bodies[static_cast<int32>(Joint.Body2)].Body->getBroadphaseHandle();
					if (Proxy1 != nullptr && Proxy2 != nullptr)
					{
						if (btBroadphasePair* Pair = World->getPairCache()->findPair(Proxy1, Proxy2))
						{
							World->getPairCache()->cleanOverlappingPair(*Pair, Dispatcher);
						}
					}
				}
			}
			else
			{
				++NumJointsWithoutEffect;   // no dynamic body: accepted, nothing to solve (docs/Abi.md 9.4 item 6)
			}
			Joints.push_back(Joint);
		}
		return PT_OK;
	}

	void FPhysScene_Bullet::DestroyJoint(FBulletJoint& Joint)
	{
		if (Joint.Constraint != nullptr)
		{
			// removeConstraint also drops the bodies' constraint refs (and with them the ignore-collision entries) [V].
			World->removeConstraint(Joint.Constraint);
			delete Joint.Constraint;
			Joint.Constraint = nullptr;
		}
		delete Joint.Feedback;
		Joint.Feedback = nullptr;
		Joint.bReleased = true;
	}

	void FPhysScene_Bullet::ReleaseJointsOf(const uint32 BodyIndex)
	{
		for (int32 Index = 0; Index < Joints.size(); ++Index)
		{
			FBulletJoint& Joint = Joints[Index];
			if (!Joint.bReleased && (Joint.Body1 == BodyIndex || Joint.Body2 == BodyIndex))
			{
				DestroyJoint(Joint);
			}
		}
	}

	void FPhysScene_Bullet::UpdateJointLevers()
	{
		// The solver builds its joint rows in convertJoints from the bodies' transforms at the start of the step (getInfo2 runs
		// after this pre-tick, before integrateTransforms [V]): the rows' angular terms are r x n with r = the pivot's offset from
		// rbA's COM at that moment (btSolverConstraint::m_relpos1CrossNormal), so that is the lever CaptureJoints undoes.
		for (int32 Index = 0; Index < Joints.size(); ++Index)
		{
			FBulletJoint& Joint = Joints[Index];
			if (Joint.bReleased || Joint.Constraint == nullptr)
			{
				continue;
			}
			const btRigidBody& RigidA = Joint.Constraint->getRigidBodyA();
			Joint.LeverAtSolve = RigidA.getCenterOfMassTransform().getBasis() * Joint.PivotInA;
		}
	}

	void FPhysScene_Bullet::CaptureJoints()
	{
		// docs/Abi.md 9.4 item 9: every live joint in JointIndex order. World frames = the bodies' actor poses at the snapshot (the
		// end of the last step, as ReadBodyStates reports them) x the ABI frames; Frame2 itself for the world. Impulses: what the
		// last solve applied to rbA = Body1, btJointFeedback's force and torque times the solver's time step, in kg*cm/s and
		// kg*cm^2/s. The feedback torque is about Body1's centre of mass, summed over the rows as r x n lambda with the lever r of
		// the solve (btSolverConstraint::m_relpos1CrossNormal [V]); abi-v1.1 reports the angular impulse about the joint anchor
		// (Frame1World's origin, item 9), so the linear impulse's moment about the COM is taken out with that same lever:
		// T_anchor = T_com - r_solve x L (M9 fix round 0: the end-of-step anchor - COM is another lever for a moving joint, and a
		// swinging ball reported |dCOM x L| instead of 0; the rows act at the pivot, so the result is the couple of the angular
		// rows). A joint whose dynamic bodies all sleep did not run: zero and PT_JOINTSNAP_SLEEPING (Bullet leaves a sleeping
		// island's feedback as it was: btSequentialImpulseConstraintSolver::convertJoints zeroes it only for the constraints it
		// solves [V]).
		const double StepDt = static_cast<double>(World->getSolverInfo().m_timeStep);
		for (int32 Index = 0; Index < Joints.size(); ++Index)
		{
			const FBulletJoint& Joint = Joints[Index];
			if (Joint.bReleased)
			{
				continue;
			}
			const FBulletBody& Record1 = Bodies[static_cast<int32>(Joint.Body1)];
			const FBulletBody* Record2 = Joint.Body2 != PT_INDEX_NONE ? &Bodies[static_cast<int32>(Joint.Body2)] : nullptr;
			PtJointSnapshot Snapshot = {};
			Snapshot.Frame1World = TransformMultiply(MakeState(Record1).ActorTransform, Joint.Frame1);
			Snapshot.Frame2World = Record2 != nullptr ? TransformMultiply(MakeState(*Record2).ActorTransform, Joint.Frame2) : Joint.Frame2;
			Snapshot.JointIndex = static_cast<uint32>(Index);
			const auto IsAsleepOrStill = [](const FBulletBody* Record) { return Record == nullptr || Record->Motion != PT_MOTION_DYNAMIC || !Record->Body->isActive(); };
			const bool bSleeping = IsAsleepOrStill(&Record1) && IsAsleepOrStill(Record2);
			if (bSleeping || Joint.Constraint == nullptr || Joint.Feedback == nullptr)
			{
				Snapshot.Flags = bSleeping ? PT_JOINTSNAP_SLEEPING : 0u;
			}
			else
			{
				Snapshot.LinearImpulse = FromBtLength(Joint.Feedback->m_appliedForceBodyA * btScalar(StepDt));   // N*s = kg*m/s -> kg*cm/s
				const btVector3 AngularImpulse = Joint.Feedback->m_appliedTorqueBodyA * btScalar(StepDt);         // kg*m^2/s, about the COM
				const PtVec3 AboutCom{ GScale.ToKgCm2(double(AngularImpulse.getX())), GScale.ToKgCm2(double(AngularImpulse.getY())),
					GScale.ToKgCm2(double(AngularImpulse.getZ())) };
				Snapshot.AngularImpulse = Sub(AboutCom, Cross(FromBtLength(Joint.LeverAtSolve), Snapshot.LinearImpulse));
			}
			SnapshotJoints.push_back(Snapshot);
		}
	}
}
