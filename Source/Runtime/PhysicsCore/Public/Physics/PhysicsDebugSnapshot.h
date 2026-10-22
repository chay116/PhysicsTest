// PhysicsDebugSnapshot.h - one untimed debug snapshot of a scene (02_Architecture 11.1, A.11). [MVP M3 read path (Jolt
// snapshot criterion); the viewer and FIslandBuilder use it from M4; the ABI structs it holds are final since ABI v1.1 (M9)]
// The ABI structs are kept as the backend wrote them: cm, kg, s, rad, and the Chaos contact convention (the normal lies on
// Body1 and points toward Body0, Phi < 0 is penetrating, Body0 is the more mobile body; ContactPoint.h:49-53 [V]).
#pragma once

#include "CoreTypes.h"
#include "PhysicsTestAbi/PtBackendAbi.h"

#include <string>
#include <vector>

namespace PhysicsTest
{
	struct FPhysicsDebugSnapshot
	{
		uint64 FrameIndex = 0;
		double SimTime = 0.0;
		uint64 Capabilities = 0;      // the backend's capabilities when captured (CONTACT_POINTS, BOUNDS, ...)
		bool bTruncated = false;      // the backend had more than the request allowed
		// M4: the backend has no snapshot (PT_E_UNSUPPORTED, e.g. Null) and the particles come from the host's pulled body
		// states instead (DebugDraw::CaptureSnapshot): no contacts, bounds and COM from the harness data model.
		bool bFromHostState = false;
		// M5 (M4 carry-over (2)): the exclusive bound of the scene's body indices when captured (the host's body count), so
		// that a consumer never sizes an array from a backend-reported index. The host capture drops particles whose
		// BodyIndex is PT_INDEX_NONE or >= NumBodies (NumRejectedParticles counts them, a backend bug logged once per scene).
		// 0 = unknown (a hand-made snapshot): GetBodyIndexBound derives it from the particles, ignoring PT_INDEX_NONE and
		// implausibly large indices (M5 round 0).
		uint32 NumBodies = 0;
		uint32 NumRejectedParticles = 0;
		// M9: joints whose JointIndex names no live joint of the scene (a backend bug, dropped and logged once per scene).
		uint32 NumRejectedJoints = 0;
		// M9: the joints come from the host's pulled body states and constraint frames (DebugDraw::CaptureSnapshot), not from
		// the backend (no PT_CAP_JOINT_SNAPSHOT): impulses are NaN with PT_JOINTSNAP_IMPULSE_NA.
		bool bJointsFromHostState = false;
		std::vector<PtParticleSnapshot> Particles;
		std::vector<PtContactPoint> Contacts;
		std::vector<PtJointSnapshot> Joints;
		// M9: parallel to Joints when the capture filled it: the index of each joint's FConstraintInstance in
		// FPhysScene::GetConstraints() (the backend's JointIndex counts only the joints it created). Empty for a hand-made
		// snapshot, whose JointIndex is read as that index.
		std::vector<int32> JointHostIndex;
		std::vector<PtDebugDrawCmd> NativeCommands;
		std::string StringPool;
		std::vector<int32> HarnessIslandIndex;   // FIslandBuilder output, parallel to Particles (M4; INDEX_NONE = no island)

		void Reset()
		{
			FrameIndex = 0;
			SimTime = 0.0;
			Capabilities = 0;
			bTruncated = false;
			bFromHostState = false;
			NumBodies = 0;
			NumRejectedParticles = 0;
			NumRejectedJoints = 0;
			bJointsFromHostState = false;
			Particles.clear();
			Contacts.clear();
			Joints.clear();
			JointHostIndex.clear();
			NativeCommands.clear();
			StringPool.clear();
			HarnessIslandIndex.clear();
		}
	};

	// A hand-made snapshot (NumBodies 0) is expected to number its bodies densely: an index at or above
	// 2 x particles + GHandMadeBodyIndexSlack is a garbage value, skipped like PT_INDEX_NONE, so it can never size an array
	// (M5 round 0: 0x7FFFFFFE would have asked for gigabytes).
	inline constexpr uint64 GHandMadeBodyIndexSlack = 64;

	// The size of a body-index-indexed array for this snapshot: NumBodies when the capture set it (the host capture always
	// does), else one past the largest plausible particle BodyIndex (hand-made snapshots: PT_INDEX_NONE and indices at or
	// above 2 x particles + GHandMadeBodyIndexSlack excluded, so the bound is O(particles)). Consumers skip particles whose
	// BodyIndex is not below it (M5: BuildParticleLookup, FIslandBuilder::Build / GatherIslands).
	inline uint32 GetBodyIndexBound(const FPhysicsDebugSnapshot& Snapshot)
	{
		if (Snapshot.NumBodies > 0)
		{
			return Snapshot.NumBodies;
		}
		const uint64 Plausible = 2u * static_cast<uint64>(Snapshot.Particles.size()) + GHandMadeBodyIndexSlack;
		uint32 Bound = 0;
		for (const PtParticleSnapshot& Particle : Snapshot.Particles)
		{
			if (Particle.BodyIndex != PT_INDEX_NONE && Particle.BodyIndex < Plausible && Particle.BodyIndex + 1 > Bound)
			{
				Bound = Particle.BodyIndex + 1;
			}
		}
		return Bound;
	}
}
