// IslandBuilder.cpp - FIslandBuilder (see IslandBuilder.h): union-find over body indices; the root of every set is its
// lowest index, so island ids do not depend on the order of the contacts.
#include "IslandBuilder.h"

#include "Physics/PhysicsDebugSnapshot.h"
#include "Physics/PhysicsInterfaceTypes.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"

#include <algorithm>

namespace PhysicsTest
{
	namespace
	{
		bool IsDynamicState(const uint8 ObjectState)
		{
			return ObjectState == static_cast<uint8>(EObjectStateType::Dynamic) || ObjectState == static_cast<uint8>(EObjectStateType::Sleeping);
		}

		struct FUnionFind
		{
			std::vector<uint32> Parent;

			explicit FUnionFind(const uint32 Num)
			{
				Parent.resize(Num);
				for (uint32 Index = 0; Index < Num; ++Index)
				{
					Parent[Index] = Index;
				}
			}
			uint32 Find(uint32 Index)
			{
				while (Parent[Index] != Index)
				{
					Parent[Index] = Parent[Parent[Index]];   // path halving
					Index = Parent[Index];
				}
				return Index;
			}
			void Union(const uint32 A, const uint32 B)
			{
				const uint32 RootA = Find(A);
				const uint32 RootB = Find(B);
				if (RootA < RootB)
				{
					Parent[RootB] = RootA;
				}
				else if (RootB < RootA)
				{
					Parent[RootA] = RootB;
				}
			}
		};
	}

	void FIslandBuilder::Build(FPhysicsDebugSnapshot& InOut, const std::span<FConstraintInstance* const> Joints)
	{
		InOut.HarnessIslandIndex.assign(InOut.Particles.size(), INDEX_NONE);
		// Sized from the snapshot's body-index bound, never from a reported index (M5): a particle outside it (PT_INDEX_NONE,
		// a garbage index) is no node and gets no island.
		const uint32 NumBodies = GetBodyIndexBound(InOut);
		if (NumBodies == 0)
		{
			return;
		}
		// Body index -> particle slot, and which bodies are island nodes.
		std::vector<int32> ParticleOfBody(NumBodies, INDEX_NONE);
		for (size_t Slot = 0; Slot < InOut.Particles.size(); ++Slot)
		{
			const uint32 BodyIndex = InOut.Particles[Slot].BodyIndex;
			if (BodyIndex < NumBodies)
			{
				ParticleOfBody[BodyIndex] = static_cast<int32>(Slot);
			}
		}
		const auto IsNode = [&](const uint32 Body)
		{
			return Body < NumBodies && ParticleOfBody[Body] != INDEX_NONE && IsDynamicState(InOut.Particles[static_cast<size_t>(ParticleOfBody[Body])].ObjectState);
		};
		FUnionFind Sets(NumBodies);
		std::vector<uint8> InGraph(NumBodies, 0);
		const auto AddEdge = [&](const uint32 Body0, const uint32 Body1)
		{
			const bool bNode0 = IsNode(Body0);
			const bool bNode1 = IsNode(Body1);
			if (bNode0)
			{
				InGraph[Body0] = 1;
			}
			if (bNode1)
			{
				InGraph[Body1] = 1;
			}
			if (bNode0 && bNode1)
			{
				Sets.Union(Body0, Body1);
			}
		};
		// An edge is a touching point (Phi <= 0) or one the solver used (PT_CONTACT_ACTIVE: an impulse > 0, M5). Jolt's
		// ACTIVE_APPROX is Phi <= 0, so its graph is unchanged; a resting PhysX point has Phi ~ +0 (float) with an impulse
		// and a Bullet point may end a step at Phi > 0 after pushing - both still join their bodies.
		for (const PtContactPoint& Contact : InOut.Contacts)
		{
			if (Contact.Phi <= 0.0 || (Contact.Flags & (PT_CONTACT_ACTIVE | PT_CONTACT_ACTIVE_APPROX)) != 0)
			{
				AddEdge(Contact.Body0, Contact.Body1);
			}
		}
		for (const FConstraintInstance* Joint : Joints)
		{
			if (Joint == nullptr)
			{
				continue;
			}
			const uint32 Body1 = Joint->Body1 != nullptr && Joint->Body1->ActorHandle.IsValid() ? Joint->Body1->ActorHandle.Index : PT_INDEX_NONE;
			const uint32 Body2 = Joint->Body2 != nullptr && Joint->Body2->ActorHandle.IsValid() ? Joint->Body2->ActorHandle.Index : PT_INDEX_NONE;
			AddEdge(Body1, Body2);
		}
		for (size_t Slot = 0; Slot < InOut.Particles.size(); ++Slot)
		{
			const uint32 Body = InOut.Particles[Slot].BodyIndex;
			if (Body < NumBodies && InGraph[Body] != 0)
			{
				InOut.HarnessIslandIndex[Slot] = static_cast<int32>(Sets.Find(Body));
			}
		}
	}

	void FIslandBuilder::GatherIslands(const FPhysicsDebugSnapshot& Snapshot, std::vector<FHarnessIsland>& OutIslands)
	{
		OutIslands.clear();
		if (Snapshot.HarnessIslandIndex.size() != Snapshot.Particles.size())
		{
			return;
		}
		// Island ids are body indices below the snapshot's bound (Build): a dense map from id to the output slot.
		const uint32 NumBodies = GetBodyIndexBound(Snapshot);
		std::vector<int32> SlotOfIsland(NumBodies, INDEX_NONE);
		for (size_t Slot = 0; Slot < Snapshot.Particles.size(); ++Slot)
		{
			const int32 Id = Snapshot.HarnessIslandIndex[Slot];
			if (Id < 0 || static_cast<uint32>(Id) >= NumBodies)
			{
				continue;
			}
			if (SlotOfIsland[static_cast<size_t>(Id)] == INDEX_NONE)
			{
				SlotOfIsland[static_cast<size_t>(Id)] = static_cast<int32>(OutIslands.size());
				FHarnessIsland& Island = OutIslands.emplace_back();
				Island.Id = Id;
			}
			FHarnessIsland& Island = OutIslands[static_cast<size_t>(SlotOfIsland[static_cast<size_t>(Id)])];
			const PtParticleSnapshot& Particle = Snapshot.Particles[Slot];
			++Island.NumParticles;
			Island.bAwake = Island.bAwake || Particle.ObjectState != static_cast<uint8>(EObjectStateType::Sleeping);
			Island.Bounds += FVector(Particle.BoundsMin.X, Particle.BoundsMin.Y, Particle.BoundsMin.Z);
			Island.Bounds += FVector(Particle.BoundsMax.X, Particle.BoundsMax.Y, Particle.BoundsMax.Z);
		}
		std::sort(OutIslands.begin(), OutIslands.end(), [](const FHarnessIsland& A, const FHarnessIsland& B) { return A.Id < B.Id; });
	}
}
