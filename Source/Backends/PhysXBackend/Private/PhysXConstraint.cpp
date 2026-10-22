// PhysXConstraint.cpp - FConstraintInstance -> PxJoint and the joint half of FPhysScene_PhysX (CreateConstraints, the
// release of a body's joints, the joint snapshot, the joint effective keys). See PhysXConstraint.h for the recipes and the
// impulse convention.
#include "PhysXConstraint.h"

#include "PhysicsInitialization.h"
#include "PhysScene_PhysX.h"
#include "PhysXSupport.h"

#include <math.h>
#include <stdio.h>

namespace PhysicsTest::PhysXBackend
{
	namespace Abi = ::PhysicsTest::Abi;

	namespace
	{
		PtQuat MultiplyQuat(const PtQuat& A, const PtQuat& B)
		{
			return PtQuat{ A.W * B.X + A.X * B.W + A.Y * B.Z - A.Z * B.Y, A.W * B.Y - A.X * B.Z + A.Y * B.W + A.Z * B.X, A.W * B.Z + A.X * B.Y - A.Y * B.X + A.Z * B.W,
				A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z };
		}

		PtVec3 RotateVector(const PtQuat& Q, const PtVec3& V)
		{
			// v' = v + 2w (q x v) + 2 q x (q x v)
			const PtVec3 T{ 2.0 * (Q.Y * V.Z - Q.Z * V.Y), 2.0 * (Q.Z * V.X - Q.X * V.Z), 2.0 * (Q.X * V.Y - Q.Y * V.X) };
			return PtVec3{ V.X + Q.W * T.X + (Q.Y * T.Z - Q.Z * T.Y), V.Y + Q.W * T.Y + (Q.Z * T.X - Q.X * T.Z), V.Z + Q.W * T.Z + (Q.X * T.Y - Q.Y * T.X) };
		}

		PtVec3 Cross(const PtVec3& A, const PtVec3& B)
		{
			return PtVec3{ A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
		}

		PtQuat Normalized(const PtQuat& Q)
		{
			const double Size = sqrt(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W);
			return Size > 0.0 ? PtQuat{ Q.X / Size, Q.Y / Size, Q.Z / Size, Q.W / Size } : PtQuat{ 0.0, 0.0, 0.0, 1.0 };
		}

		const char* GetTypeName(const uint32 Type)
		{
			return Type == PT_JOINT_BALL ? "ball" : "hinge";
		}
	}

	PtTransform ComposeTransforms(const PtTransform& Parent, const PtTransform& Local)
	{
		const PtVec3 Offset = RotateVector(Parent.Rotation, Local.Translation);
		return PtTransform{ Normalized(MultiplyQuat(Parent.Rotation, Local.Rotation)),
			PtVec3{ Parent.Translation.X + Offset.X, Parent.Translation.Y + Offset.Y, Parent.Translation.Z + Offset.Z } };
	}

	physx::PxJoint* CreateJoint_AssumesLocked(physx::PxPhysics& Physics, const FPhysXSceneConfig& Config, const Abi::FUnitScale& Scale, const PtConstraintCreateInfo& Info,
		physx::PxRigidActor& Child, physx::PxRigidActor* Parent, const bool bVisualize)
	{
		// UE4 [M]: PxD6JointCreate(SDK, actor2, frame2, actor1, frame1) - the parent first (JointActorOrder=ParentFirst).
		const bool bParentFirst = Config.JointActorOrder == EPhysXJointActorOrder::ParentFirst;
		physx::PxRigidActor* const Actor0 = bParentFirst ? Parent : &Child;
		physx::PxRigidActor* const Actor1 = bParentFirst ? &Child : Parent;
		const physx::PxTransform Frame0 = U2PTransform(bParentFirst ? Info.Frame2 : Info.Frame1, Scale);
		const physx::PxTransform Frame1 = U2PTransform(bParentFirst ? Info.Frame1 : Info.Frame2, Scale);
		physx::PxJoint* Joint = nullptr;
		if (Config.JointRecipe == EPhysXJointRecipe::D6)
		{
			physx::PxD6Joint* D6 = physx::PxD6JointCreate(Physics, Actor0, Frame0, Actor1, Frame1);
			if (D6 != nullptr)
			{
				// ConstraintUtils::ConfigureAsBallAndSocket / ConfigureAsHinge as D6 motions (docs/Abi.md 9.4 item 1): the linear
				// axes locked (the D6 default, stated anyway), twist free for both, the swings free for a ball, locked for a hinge.
				D6->setMotion(physx::PxD6Axis::eX, physx::PxD6Motion::eLOCKED);
				D6->setMotion(physx::PxD6Axis::eY, physx::PxD6Motion::eLOCKED);
				D6->setMotion(physx::PxD6Axis::eZ, physx::PxD6Motion::eLOCKED);
				const physx::PxD6Motion::Enum Swing = Info.Type == PT_JOINT_BALL ? physx::PxD6Motion::eFREE : physx::PxD6Motion::eLOCKED;
				D6->setMotion(physx::PxD6Axis::eTWIST, physx::PxD6Motion::eFREE);
				D6->setMotion(physx::PxD6Axis::eSWING1, Swing);
				D6->setMotion(physx::PxD6Axis::eSWING2, Swing);
			}
			Joint = D6;
		}
		else if (Info.Type == PT_JOINT_BALL)
		{
			Joint = physx::PxSphericalJointCreate(Physics, Actor0, Frame0, Actor1, Frame1);
		}
		else
		{
			Joint = physx::PxRevoluteJointCreate(Physics, Actor0, Frame0, Actor1, Frame1);
		}
		if (Joint == nullptr)
		{
			return nullptr;
		}
		// bDisableCollision (docs/Abi.md 9.4 item 5): PhysX's default is "no contacts between the jointed pair", so the flag is
		// raised exactly when the ABI flag is clear (UE's default). A world joint holds it too; it changes nothing there.
		Joint->setConstraintFlag(physx::PxConstraintFlag::eCOLLISION_ENABLED, (Info.Flags & PT_JOINTF_DISABLE_COLLISION) == 0);
		Joint->setConstraintFlag(physx::PxConstraintFlag::eVISUALIZATION, bVisualize);
		return Joint;
	}

	bool JointHoldsRecipe(const FPhysXJoint& Record, const physx::PxRigidActor* Body1Actor, const physx::PxRigidActor* Body2Actor)
	{
		if (Record.Joint == nullptr)
		{
			return true;   // no engine joint by design (no dynamic body): nothing to hold
		}
		const physx::PxJoint& Joint = *Record.Joint;
		const physx::PxJointConcreteType::Enum Expected = Record.Recipe == EPhysXJointRecipe::D6 ? physx::PxJointConcreteType::eD6
			: (Record.Type == PT_JOINT_BALL ? physx::PxJointConcreteType::eSPHERICAL : physx::PxJointConcreteType::eREVOLUTE);
		if (Joint.getConcreteType() != static_cast<physx::PxU16>(Expected))
		{
			return false;
		}
		physx::PxRigidActor* Actor0 = nullptr;
		physx::PxRigidActor* Actor1 = nullptr;
		Joint.getActors(Actor0, Actor1);
		const physx::PxRigidActor* Want0 = Record.bParentFirst ? Body2Actor : Body1Actor;
		const physx::PxRigidActor* Want1 = Record.bParentFirst ? Body1Actor : Body2Actor;
		if (Actor0 != Want0 || Actor1 != Want1)
		{
			return false;
		}
		const bool bCollision = (Joint.getConstraintFlags() & physx::PxConstraintFlag::eCOLLISION_ENABLED) ? true : false;
		if (bCollision != ((Record.Flags & PT_JOINTF_DISABLE_COLLISION) == 0))
		{
			return false;
		}
		if (Record.Recipe == EPhysXJointRecipe::D6)
		{
			const physx::PxD6Joint& D6 = static_cast<const physx::PxD6Joint&>(Joint);
			const physx::PxD6Motion::Enum Swing = Record.Type == PT_JOINT_BALL ? physx::PxD6Motion::eFREE : physx::PxD6Motion::eLOCKED;
			return D6.getMotion(physx::PxD6Axis::eX) == physx::PxD6Motion::eLOCKED && D6.getMotion(physx::PxD6Axis::eY) == physx::PxD6Motion::eLOCKED
				&& D6.getMotion(physx::PxD6Axis::eZ) == physx::PxD6Motion::eLOCKED && D6.getMotion(physx::PxD6Axis::eTWIST) == physx::PxD6Motion::eFREE
				&& D6.getMotion(physx::PxD6Axis::eSWING1) == Swing && D6.getMotion(physx::PxD6Axis::eSWING2) == Swing;
		}
		return true;
	}

	// ---- FPhysScene_PhysX: joints ------------------------------------------------------------------------------------------
	PtResult FPhysScene_PhysX::CreateConstraints(const Abi::TStridedView<PtConstraintCreateInfo> Infos)
	{
		// The kit checked every argument (docs/Abi.md 9.4 item 7): known type, Body1 a live body, Body2 a live other body or the
		// world, known flags, Reserved 0, unit frames. Joint i of this call gets index Joints.size() + i (= GetNumJoints() + i).
		physx::PxPhysics& Physics = GetPhysXSDK();
		const uint32 First = Joints.size();
		const uint32 Num = Infos.Num();
		Joints.reserve(First + Num);
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			const PtConstraintCreateInfo& Info = Infos[Index];
			FPhysXJoint Record;
			Record.Frame1 = Info.Frame1;
			Record.Frame2 = Info.Frame2;
			Record.Type = Info.Type;
			Record.Body1 = Info.Body1;
			Record.Body2 = Info.Body2;
			Record.Flags = Info.Flags;
			Record.CreatedAtStep = TotalSteps;
			Record.Recipe = Config.JointRecipe;
			Record.bParentFirst = Config.JointActorOrder == EPhysXJointActorOrder::ParentFirst;
			const FPhysXBody& Child = Bodies[Info.Body1];
			const FPhysXBody* Parent = Info.Body2 != PT_INDEX_NONE ? &Bodies[Info.Body2] : nullptr;
			const bool bAnyDynamic = Child.IsSimulatedDynamic() || (Parent != nullptr && Parent->IsSimulatedDynamic());
			if (bAnyDynamic)
			{
				// Viz runs mark the joint for eJOINT_LOCAL_FRAMES / eJOINT_LIMITS (PxConstraintFlag::eVISUALIZATION).
				const bool bVisualize = bVisualization && (Config.Visualization & (PhysXViz_JointLocalFrames | PhysXViz_JointLimits)) != 0;
				Record.Joint = CreateJoint_AssumesLocked(Physics, Config, Scale, Info, *Child.Actor, Parent != nullptr ? Parent->Actor : nullptr, bVisualize);
				if (Record.Joint == nullptr)
				{
					// All or nothing (docs/Abi.md 9.4 item 6): release what this call created.
					while (Joints.size() > First)
					{
						if (Joints.back().Joint != nullptr)
						{
							Joints.back().Joint->release();
						}
						NumJointsWithoutDynamicBody -= Joints.back().Joint == nullptr ? 1u : 0u;
						Joints.popBack();
					}
					CheckPhysXErrors(true);
					return GetKit().SetLastError(PT_E_ENGINE, "PhysX: joint %u of the call (%s, Body1 %u, Body2 %d): %s create failed", Index, GetTypeName(Info.Type), Info.Body1,
						Info.Body2 == PT_INDEX_NONE ? -1 : static_cast<int>(Info.Body2), Config.JointRecipe == EPhysXJointRecipe::D6 ? "PxD6JointCreate"
						: (Info.Type == PT_JOINT_BALL ? "PxSphericalJointCreate" : "PxRevoluteJointCreate"));
				}
				Record.Joint->userData = FPhysxUserData::Encode(EPhysxUserDataType::Joint, First + Index);
			}
			else
			{
				++NumJointsWithoutDynamicBody;   // accepted, no effect (docs/Abi.md 9.4 item 6)
			}
			Joints.pushBack(Record);
		}
		SnapshotJoints.reserve(Joints.size());
		CheckPhysXErrors(true);
		return PT_OK;
	}

	void FPhysScene_PhysX::ReleaseJointsOf(const uint32 BodyIndex)
	{
		// docs/Abi.md 9.4 item 6: releasing a body releases every joint attached to it; the index stays taken. PhysX does not
		// release a joint with its actor, so the joint goes first.
		for (FPhysXJoint& Record : Joints)
		{
			if (Record.bReleased || (Record.Body1 != BodyIndex && Record.Body2 != BodyIndex))
			{
				continue;
			}
			if (Record.Joint != nullptr)
			{
				Record.Joint->release();
				Record.Joint = nullptr;
			}
			Record.bReleased = true;
			++NumJointsReleasedWithBodies;
		}
	}

	void FPhysScene_PhysX::ReleaseAllJoints()
	{
		for (FPhysXJoint& Record : Joints)
		{
			if (Record.Joint != nullptr)
			{
				Record.Joint->release();
				Record.Joint = nullptr;
			}
		}
	}

	PtTransform FPhysScene_PhysX::GetActorPose(const uint32 BodyIndex) const
	{
		if (BodyIndex == PT_INDEX_NONE)
		{
			return PtTransform{ PtQuat{ 0.0, 0.0, 0.0, 1.0 }, PtVec3{ 0.0, 0.0, 0.0 } };
		}
		const physx::PxTransform Pose = Bodies[BodyIndex].Actor->getGlobalPose();
		return P2UTransform(physx::PxTransform(Pose.p, Pose.q.getNormalized()), Scale);
	}

	PtJointSnapshot FPhysScene_PhysX::MakeJointSnapshot(const uint32 JointIndex) const
	{
		const FPhysXJoint& Record = Joints[JointIndex];
		PtJointSnapshot Snapshot = {};
		Snapshot.JointIndex = JointIndex;
		// The joint's local frames as the engine holds them (PxJoint::getLocalPose, actor space, native units), else the ABI frames.
		PtTransform Local1 = Record.Frame1;
		PtTransform Local2 = Record.Frame2;
		if (Record.Joint != nullptr)
		{
			const physx::PxJointActorIndex::Enum Child = Record.bParentFirst ? physx::PxJointActorIndex::eACTOR1 : physx::PxJointActorIndex::eACTOR0;
			const physx::PxJointActorIndex::Enum Parent = Record.bParentFirst ? physx::PxJointActorIndex::eACTOR0 : physx::PxJointActorIndex::eACTOR1;
			Local1 = P2UTransform(Record.Joint->getLocalPose(Child), Scale);
			Local2 = P2UTransform(Record.Joint->getLocalPose(Parent), Scale);
		}
		// docs/Abi.md 9.4 item 9: Body1's actor pose x Frame1, Body2's x Frame2 (the world: Frame2 itself), at the snapshot pose.
		Snapshot.Frame1World = ComposeTransforms(GetActorPose(Record.Body1), Local1);
		Snapshot.Frame2World = Record.Body2 == PT_INDEX_NONE ? PtTransform{ Normalized(Local2.Rotation), Local2.Translation } : ComposeTransforms(GetActorPose(Record.Body2), Local2);
		// Every dynamic body asleep (or none at all): the joint did not run - zero impulses and PT_JOINTSNAP_SLEEPING.
		const auto IsAwakeDynamic = [this](const uint32 Index) { return Index != PT_INDEX_NONE && Bodies[Index].IsSimulatedDynamic() && !IsAsleep(Bodies[Index]); };
		const bool bRan = IsAwakeDynamic(Record.Body1) || IsAwakeDynamic(Record.Body2);
		if (!bRan)
		{
			Snapshot.Flags |= PT_JOINTSNAP_SLEEPING;
			return Snapshot;
		}
		if (Record.Joint == nullptr || TotalSteps == Record.CreatedAtStep || !(LastSimulatedDeltaTime > 0.0f))
		{
			return Snapshot;   // no step has run the joint yet: zero impulses
		}
		// PxConstraint::getForce = the last step's writeback impulse on actor0 / dt, the torque about actor1's frame origin
		// (see PhysXConstraint.h).
		physx::PxVec3 Force(0.0f);
		physx::PxVec3 Torque(0.0f);
		Record.Joint->getConstraint()->getForce(Force, Torque);
		const double Dt = static_cast<double>(LastSimulatedDeltaTime);
		const double Sign = Record.bParentFirst ? -1.0 : 1.0;   // actor0 = Body2: Body1 got the opposite
		const PtVec3 Linear{ Sign * Scale.ToCm(static_cast<double>(Force.x)) * Dt, Sign * Scale.ToCm(static_cast<double>(Force.y)) * Dt,
			Sign * Scale.ToCm(static_cast<double>(Force.z)) * Dt };
		PtVec3 Angular{ Sign * Scale.ToKgCm2(static_cast<double>(Torque.x)) * Dt, Sign * Scale.ToKgCm2(static_cast<double>(Torque.y)) * Dt,
			Sign * Scale.ToKgCm2(static_cast<double>(Torque.z)) * Dt };
		if (!Record.bParentFirst)
		{
			// The torque is about actor1's (Body2's) frame origin: move it to Frame1World's, T1 = T2 + (P2 - P1) x F.
			const PtVec3 Arm{ Snapshot.Frame2World.Translation.X - Snapshot.Frame1World.Translation.X, Snapshot.Frame2World.Translation.Y - Snapshot.Frame1World.Translation.Y,
				Snapshot.Frame2World.Translation.Z - Snapshot.Frame1World.Translation.Z };
			const PtVec3 Moment = Cross(Arm, Linear);
			Angular = PtVec3{ Angular.X + Moment.X, Angular.Y + Moment.Y, Angular.Z + Moment.Z };
		}
		Snapshot.LinearImpulse = Linear;
		Snapshot.AngularImpulse = Angular;
		if (Scene->getSolverType() == physx::PxSolverType::eTGS)
		{
			// TGS's writeback carries no usable torque (PhysX 5.11 [V]): writeBack1DStep sums (ang0 + lin0 x rAWorld) x impulse
			// (DyTGSContactPrep.cpp:2830), but TGS zeroes ang0 of the linear rows and recomputes r0 x lin0 per iteration
			// (:2055-2062), so the linear rows add -rA x F instead of +rA x F and the later -body0WorldOffset x F (:2835) doubles
			// it; the angular rows' share is missing from the sum (measured: a 100 cm cantilever on a vertical hinge reports
			// -2 (r x F) and none of its -d m g dt). The linear sum is exact (lin0 is untouched), so only the angular impulse is
			// unknown here: NaN with PT_JOINTSNAP_ANGULAR_IMPULSE_NA (abi-v1.1, PtJointSnapshot, docs/Abi.md 9.4 item 9).
			const double NaN = nan("");
			Snapshot.AngularImpulse = PtVec3{ NaN, NaN, NaN };
			Snapshot.Flags |= PT_JOINTSNAP_ANGULAR_IMPULSE_NA;
		}
		return Snapshot;
	}

	void FPhysScene_PhysX::StageJointSnapshots()
	{
		// docs/Abi.md 9.4 item 9: one record per live joint, in JointIndex order; a released joint is not reported.
		for (uint32 Index = 0; Index < Joints.size(); ++Index)
		{
			if (!Joints[Index].bReleased)
			{
				SnapshotJoints.pushBack(MakeJointSnapshot(Index));
			}
		}
	}

	void FPhysScene_PhysX::AppendJointSettings(Abi::FTextWriter& Out) const
	{
		const double NaN = nan("");
		char Text[512];
		// UE4's joint projection (on by default [M]) does not exist in PhysX 5.11 (docs/Abi.md 9.4 item 4, optional key).
		Out.AppendEffective("joint.projection", "0: PhysX 5.11 has no joint projection (removed in 5.2.0, CHANGELOG.md:1703; no PxConstraintFlag for it); UE4's FConstraintInstance "
			"projected by default [M], not reproducible here (by-design)", "bool", 0.0, "bool", "COMPILE_TIME");
		uint32 NumLive = 0;
		uint32 NumEngine = 0;
		uint32 NumBall = 0;
		uint32 NumHinge = 0;
		uint32 NumCollision = 0;
		uint32 NumHolding = 0;
		for (const FPhysXJoint& Record : Joints)
		{
			if (Record.bReleased)
			{
				continue;
			}
			++NumLive;
			NumBall += Record.Type == PT_JOINT_BALL ? 1u : 0u;
			NumHinge += Record.Type == PT_JOINT_HINGE ? 1u : 0u;
			if (Record.Joint == nullptr)
			{
				++NumHolding;
				continue;
			}
			++NumEngine;
			NumCollision += (Record.Joint->getConstraintFlags() & physx::PxConstraintFlag::eCOLLISION_ENABLED) ? 1u : 0u;
			const physx::PxRigidActor* Body2Actor = Record.Body2 != PT_INDEX_NONE ? Bodies[Record.Body2].Actor : nullptr;
			NumHolding += JointHoldsRecipe(Record, Bodies[Record.Body1].Actor, Body2Actor) ? 1u : 0u;
		}
		const char* const RecipeText = Config.JointRecipe == EPhysXJointRecipe::D6 ? "PxD6Joint (linear locked; ball: twist / swing1 / swing2 free; hinge: twist free)"
			: "PxSphericalJoint (ball) / PxRevoluteJoint (hinge about frame X)";
		snprintf(Text, sizeof(Text), "%s, actor0 = %s, solved by the scene's %s solver with the bodies' iterations; hard joints (no limit, drive, break or projection)",
			RecipeText, Config.JointActorOrder == EPhysXJointActorOrder::ParentFirst ? "Body2 (the parent, UE4 order)" : "Body1 (the child)", ToString(Scene->getSolverType()));
		Out.AppendEffective("joint.solver", Text, "", NaN, "", "SET_ONLY");
		// Checked against the engine's joints (getConcreteType, getActors, getMotion, getConstraintFlags); canonical = the joints
		// that do not hold the variant's recipe (0 = all do). SET_ONLY until G9 has a rule for it (docs/Abi.md 9.1 decision 11).
		snprintf(Text, sizeof(Text), "%u live joints (%u ball, %u hinge): %u PhysX joints (JointRecipe=%s, JointActorOrder=%s), %u of them with eCOLLISION_ENABLED; %u "
			"without a dynamic body (no engine joint); %u of %u hold the recipe; %u released with their bodies", NumLive, NumBall, NumHinge, NumEngine,
			ToString(Config.JointRecipe), ToString(Config.JointActorOrder), NumCollision, NumJointsWithoutDynamicBody, NumHolding, NumLive, NumJointsReleasedWithBodies);
		Out.AppendEffective("physx.joints", Text, "count", static_cast<double>(NumLive - NumHolding), "count", "SET_ONLY");
	}
}
