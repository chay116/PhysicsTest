// PhysicsDebugSnapshot.h - one untimed debug snapshot of a scene (02_Architecture 11.1, A.11). [MVP M3 read path (Jolt
// snapshot criterion); the viewer and FIslandBuilder use it from M4; the ABI structs it holds are PROVISIONAL(M9)]
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
		std::vector<PtParticleSnapshot> Particles;
		std::vector<PtContactPoint> Contacts;
		std::vector<PtJointSnapshot> Joints;
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
			Particles.clear();
			Contacts.clear();
			Joints.clear();
			NativeCommands.clear();
			StringPool.clear();
			HarnessIslandIndex.clear();
		}
	};
}
