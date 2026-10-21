// PhysXSimEventCallback.h - UE4's FPhysXSimEventCallback, viz only (02_Architecture 8.2, 9.4: no event callback and no
// eNOTIFY_* pair flag in bench runs; the effective key listeners.installed records it). [MVP M5]   PT_BACKEND_NS=PhysX
//
// PhysX calls it from fetchResults on the thread that calls fetchResults (the owner thread, in WaitPhysScenes), so it
// may touch the scene's arrays; it still never allocates: the point array is sized once and points past the end are
// dropped and counted (the snapshot is then marked truncated), as the Jolt listener does.
// onContact keeps at most 16 points per pair [M] (UE4's per-pair bound), in PhysX's own terms (native units): the
// normal points from the pair's second shape to its first (PxSimulationEventCallback.h:447 [V]), separation < 0 is
// penetrating. onWake / onSleep only count (actors carry eSEND_SLEEP_NOTIFIES in viz runs).
#pragma once

#include "PhysXSupport.h"

namespace PhysicsTest::PhysXBackend
{
	struct FPhysXRawContact
	{
		uint32 Body0 = 0;   // the pair's first actor (PxContactPairHeader::actors[0]) as a harness body index
		uint32 Body1 = 0;
		uint16 Shape0 = 0;  // harness element index of the pair's first shape (PxShape::userData)
		uint16 Shape1 = 0;
		double Position[3] = {};   // native units, world
		double Normal[3] = {};     // from shape 1 toward shape 0
		double Separation = 0.0;   // native units, < 0 penetrating
		double NormalImpulse = 0.0;   // impulse . normal of the step's solver (native kg units/s; 0 = not in the solver)
	};

	inline constexpr uint32 GPhysXMaxPointsPerPair = 16;   // [M]

	class FPhysXSimEventCallback final : public physx::PxSimulationEventCallback
	{
	public:
		explicit FPhysXSimEventCallback(uint32 Capacity);

		void onConstraintBreak(physx::PxConstraintInfo* /*Constraints*/, physx::PxU32 /*Count*/) override {}
		void onWake(physx::PxActor** Actors, physx::PxU32 Count) override;
		void onSleep(physx::PxActor** Actors, physx::PxU32 Count) override;
		void onContact(const physx::PxContactPairHeader& PairHeader, const physx::PxContactPair* Pairs, physx::PxU32 NumPairs) override;
		void onTrigger(physx::PxTriggerPair* /*Pairs*/, physx::PxU32 /*Count*/) override {}
		void onAdvance(const physx::PxRigidBody* const* /*Bodies*/, const physx::PxTransform* /*Poses*/, const physx::PxU32 /*Count*/) override {}

		// The scene clears before each simulate: a snapshot shows the contacts of the last step.
		void BeginStep();
		uint32 GetNumContacts() const { return static_cast<uint32>(Contacts.size()); }
		const FPhysXRawContact* GetContacts() const { return Contacts.begin(); }
		uint32 GetNumPairs() const { return NumPairs; }
		bool HasDropped() const { return NumDropped != 0; }
		uint64 GetNumWakeEvents() const { return NumWakeEvents; }
		uint64 GetNumSleepEvents() const { return NumSleepEvents; }
		uint64 GetNumOverPairLimit() const { return NumOverPairLimit; }

	private:
		physx::PxArray<FPhysXRawContact> Contacts;   // reserved once (Capacity); never grows
		uint32 Capacity = 0;
		uint32 NumPairs = 0;
		uint32 NumDropped = 0;
		uint64 NumOverPairLimit = 0;   // points beyond GPhysXMaxPointsPerPair of a pair, over the run
		uint64 NumWakeEvents = 0;
		uint64 NumSleepEvents = 0;
	};
}
