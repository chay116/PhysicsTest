// JoltContactListener.h - the viz-only contact listener of the Jolt debug snapshot (02_Architecture 8.3, 11.1). [MVP M3]
// Installed only in viz runs or with PT_INSTR_CONTACTS (bench runs must not pay for a listener, 02 9.4; the effective key
// listeners.installed records it). It records each manifold of the step in Jolt's own terms (metres); the scene turns
// them into the Chaos convention when a snapshot is captured.
//
// Jolt calls the listener from its job threads. The listener never allocates there and takes no lock: the scene sizes
// the contact array once (4 points x the contact constraint capacity, Jolt reduces a manifold to at most 4 points), each
// manifold reserves its slots with one atomic add, and points past the end are dropped and counted (the snapshot is then
// marked truncated). The container is a JPH::Array, never a std:: container: an adapter TU is compiled with Jolt's
// /arch:AVX2, and a std::vector instantiation would be a COMDAT shared with the harness (docs/Abi.md 5.13).
#pragma once

#include "JoltIncludes.h"
#include "PtTypes.h"

#include <atomic>

namespace PhysicsTest::JoltBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	// One Jolt contact point as reported: body 1 / body 2 of the callback, the world-space points on each body, the
	// manifold normal (Jolt: the direction that moves body 2 out of collision, i.e. from body 1 toward body 2) and the
	// penetration depth (> 0 penetrating, < 0 speculative), all in metres.
	struct FJoltRawContact
	{
		uint32 Body1 = 0;
		uint32 Body2 = 0;
		double PointOn1[3] = {};
		double PointOn2[3] = {};
		double Normal[3] = {};
		double PenetrationDepth = 0.0;
	};

	class FJoltContactListener final : public JPH::ContactListener
	{
	public:
		explicit FJoltContactListener(const uint32 Capacity)
		{
			Contacts.resize(Capacity);
		}

		void OnContactAdded(const JPH::Body& Body1, const JPH::Body& Body2, const JPH::ContactManifold& Manifold, JPH::ContactSettings& /*Settings*/) override
		{
			Record(Body1, Body2, Manifold);
		}
		void OnContactPersisted(const JPH::Body& Body1, const JPH::Body& Body2, const JPH::ContactManifold& Manifold, JPH::ContactSettings& /*Settings*/) override
		{
			Record(Body1, Body2, Manifold);
		}

		// The scene clears before each step (owner thread, no job running): a snapshot shows the contacts of the last step.
		void BeginStep()
		{
			NumReserved.store(0, std::memory_order_relaxed);
			NumManifolds.store(0, std::memory_order_relaxed);
			NumDropped.store(0, std::memory_order_relaxed);
		}
		// Only between steps (the owner thread, no job running).
		uint32 GetNumContacts() const
		{
			const uint32 Reserved = NumReserved.load(std::memory_order_relaxed);
			const uint32 Capacity = static_cast<uint32>(Contacts.size());
			return Reserved < Capacity ? Reserved : Capacity;
		}
		const FJoltRawContact* GetContacts() const { return Contacts.data(); }
		uint32 GetNumManifolds() const { return NumManifolds.load(std::memory_order_relaxed); }
		// True when the last step had more contact points than the array holds (the snapshot is then truncated).
		bool HasDropped() const { return NumDropped.load(std::memory_order_relaxed) != 0; }

	private:
		void Record(const JPH::Body& Body1, const JPH::Body& Body2, const JPH::ContactManifold& Manifold)
		{
			const uint32 Index1 = static_cast<uint32>(Body1.GetUserData() & 0xFFFFFFFFull);
			const uint32 Index2 = static_cast<uint32>(Body2.GetUserData() & 0xFFFFFFFFull);
			NumManifolds.fetch_add(1, std::memory_order_relaxed);
			const uint32 NumPoints = static_cast<uint32>(Manifold.mRelativeContactPointsOn1.size());
			const uint32 First = NumReserved.fetch_add(NumPoints, std::memory_order_relaxed);
			const uint32 Capacity = static_cast<uint32>(Contacts.size());
			for (uint32 Point = 0; Point < NumPoints; ++Point)
			{
				const uint32 Slot = First + Point;
				if (Slot >= Capacity)
				{
					NumDropped.fetch_add(NumPoints - Point, std::memory_order_relaxed);
					return;
				}
				const JPH::RVec3 On1 = Manifold.GetWorldSpaceContactPointOn1(Point);
				const JPH::RVec3 On2 = Manifold.GetWorldSpaceContactPointOn2(Point);
				FJoltRawContact& Contact = Contacts[Slot];
				Contact.Body1 = Index1;
				Contact.Body2 = Index2;
				Contact.PointOn1[0] = static_cast<double>(On1.GetX());
				Contact.PointOn1[1] = static_cast<double>(On1.GetY());
				Contact.PointOn1[2] = static_cast<double>(On1.GetZ());
				Contact.PointOn2[0] = static_cast<double>(On2.GetX());
				Contact.PointOn2[1] = static_cast<double>(On2.GetY());
				Contact.PointOn2[2] = static_cast<double>(On2.GetZ());
				Contact.Normal[0] = static_cast<double>(Manifold.mWorldSpaceNormal.GetX());
				Contact.Normal[1] = static_cast<double>(Manifold.mWorldSpaceNormal.GetY());
				Contact.Normal[2] = static_cast<double>(Manifold.mWorldSpaceNormal.GetZ());
				Contact.PenetrationDepth = static_cast<double>(Manifold.mPenetrationDepth);
			}
		}

		JPH::Array<FJoltRawContact> Contacts;   // sized once; job threads write disjoint slots
		std::atomic<uint32> NumReserved{ 0 };
		std::atomic<uint32> NumManifolds{ 0 };
		std::atomic<uint32> NumDropped{ 0 };
	};
}
