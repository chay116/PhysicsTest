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
