// PhysXSimEventCallback.cpp - see PhysXSimEventCallback.h.
#include "PhysXSimEventCallback.h"

namespace PhysicsTest::PhysXBackend
{
	FPhysXSimEventCallback::FPhysXSimEventCallback(const uint32 InCapacity)
		: Capacity(InCapacity)
	{
		Contacts.reserve(InCapacity);
	}

	void FPhysXSimEventCallback::BeginStep()
	{
		Contacts.clear();   // keeps the reserved capacity
		NumPairs = 0;
		NumDropped = 0;
	}

	void FPhysXSimEventCallback::onWake(physx::PxActor** /*Actors*/, const physx::PxU32 Count)
	{
		NumWakeEvents += Count;
	}

	void FPhysXSimEventCallback::onSleep(physx::PxActor** /*Actors*/, const physx::PxU32 Count)
	{
		NumSleepEvents += Count;
	}

	void FPhysXSimEventCallback::FindPointOnSecondShape(const physx::PxShape& Shape, const physx::PxRigidActor& Actor, const uint32 ActorIndex,
		const physx::PxContactPairPoint& Point, FPhysXRawContact& InOut)
	{
		InOut.PointOn1[0] = InOut.Position[0];
		InOut.PointOn1[1] = InOut.Position[1];
		InOut.PointOn1[2] = InOut.Position[2];
		// The pose of the step's narrow phase: the pre-step actor pose (a dynamic second shape moved during the solve).
		const bool bPreStep = PreStepPoses != nullptr && ActorIndex < NumPreStepPoses && Actor.is<physx::PxRigidDynamic>() != nullptr;
		const physx::PxTransform ShapePose = (bPreStep ? PreStepPoses[ActorIndex] : Actor.getGlobalPose()) * Shape.getLocalPose();
		// From outside the shape: the reported point lies within |separation| of its surface along the normal (plus the
		// contact offsets of a speculative point), and the normal points out of it (PxSimulationEventCallback.h:447 [V]).
		const physx::PxReal Reach = physx::PxAbs(Point.separation) + 2.0f * Shape.getContactOffset() + 1e-2f;
		const physx::PxVec3 Origin = Point.position + Point.normal * Reach;
		// The closest point of the shape to that outside origin: on a face it is the point under the origin along the normal,
		// on a curved surface or an edge the point on the normal line (a ray would graze an edge and miss).
		physx::PxVec3 Closest(0.0f);
		const physx::PxReal Distance = physx::PxGeometryQuery::pointDistance(Origin, Shape.getGeometry(), ShapePose, &Closest);
		if (!(Distance > 0.0f))
		{
			++NumSurfaceFallbacks;   // the origin was inside the shape: keep the reported point
			return;
		}
		const physx::PxVec3 Hit = Closest;
		InOut.PointOn1[0] = static_cast<double>(Hit.x);
		InOut.PointOn1[1] = static_cast<double>(Hit.y);
		InOut.PointOn1[2] = static_cast<double>(Hit.z);
	}

	void FPhysXSimEventCallback::onContact(const physx::PxContactPairHeader& PairHeader, const physx::PxContactPair* Pairs, const physx::PxU32 InNumPairs)
	{
		if (PairHeader.flags & (physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_0 | physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
		{
			return;
		}
		const FPhysxUserData Actor0 = FPhysxUserData::Decode(PairHeader.actors[0]->userData);
		const FPhysxUserData Actor1 = FPhysxUserData::Decode(PairHeader.actors[1]->userData);
		if (Actor0.Type != EPhysxUserDataType::Body || Actor1.Type != EPhysxUserDataType::Body)
		{
			return;
		}
		physx::PxContactPairPoint Points[GPhysXMaxPointsPerPair];
		for (physx::PxU32 PairIndex = 0; PairIndex < InNumPairs; ++PairIndex)
		{
			const physx::PxContactPair& Pair = Pairs[PairIndex];
			if (Pair.flags & (physx::PxContactPairFlag::eREMOVED_SHAPE_0 | physx::PxContactPairFlag::eREMOVED_SHAPE_1))
			{
				continue;
			}
			const physx::PxU32 NumPoints = Pair.extractContacts(Points, GPhysXMaxPointsPerPair);
			if (NumPoints == 0)
			{
				continue;
			}
			++NumPairs;
			const uint16 Shape0 = static_cast<uint16>(FPhysxUserData::Decode(Pair.shapes[0]->userData).Index);
			const uint16 Shape1 = static_cast<uint16>(FPhysxUserData::Decode(Pair.shapes[1]->userData).Index);
			for (physx::PxU32 Point = 0; Point < NumPoints; ++Point)
			{
				if (Contacts.size() >= Capacity)
				{
					NumDropped += NumPoints - Point;   // never grow: the array is sized once
					break;
				}
				FPhysXRawContact Contact;
				Contact.Body0 = Actor0.Index;
				Contact.Body1 = Actor1.Index;
				Contact.Shape0 = Shape0;
				Contact.Shape1 = Shape1;
				Contact.Position[0] = static_cast<double>(Points[Point].position.x);
				Contact.Position[1] = static_cast<double>(Points[Point].position.y);
				Contact.Position[2] = static_cast<double>(Points[Point].position.z);
				Contact.Normal[0] = static_cast<double>(Points[Point].normal.x);
				Contact.Normal[1] = static_cast<double>(Points[Point].normal.y);
				Contact.Normal[2] = static_cast<double>(Points[Point].normal.z);
				Contact.Separation = static_cast<double>(Points[Point].separation);
				if (const physx::PxRigidActor* Rigid1 = PairHeader.actors[1]->is<physx::PxRigidActor>())
				{
					FindPointOnSecondShape(*Pair.shapes[1], *Rigid1, Actor1.Index, Points[Point], Contact);
				}
				else
				{
					Contact.PointOn1[0] = Contact.Position[0];
					Contact.PointOn1[1] = Contact.Position[1];
					Contact.PointOn1[2] = Contact.Position[2];
				}
				Contact.NormalImpulse = static_cast<double>(Points[Point].impulse.dot(Points[Point].normal));
				Contacts.pushBack(Contact);
			}
			if (Pair.contactCount > GPhysXMaxPointsPerPair)
			{
				NumOverPairLimit += Pair.contactCount - GPhysXMaxPointsPerPair;   // the [M] 16-point bound of a pair (by design, not truncation)
			}
		}
	}
}
