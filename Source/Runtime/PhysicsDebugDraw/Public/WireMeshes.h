// WireMeshes.h - the unit wire meshes of the debug draw renderer (02_Architecture 11.1 tessellation table). [MVP M4]
// Renderer-independent so that the segment counts are unit tests in every preset (the D3D11 renderer instances these
// meshes; Progress.md M4 records the move out of D3D11DebugRenderer/Private). Topology of UE's line batcher:
//   box      12 segments                         ULineBatchComponent::DrawBox (LineBatchComponent.cpp:236-288 [V])
//   sphere   2 * S * S segments, S = 8 -> 128    DrawSphere (LineBatchComponent.cpp:426-466 [V]); S = the ChaosDD renderer's
//                                                SphereSegments(8) (ChaosDebugDrawSubsystem.cpp:58 [V])
//   capsule  2 circles + 4 half circles + 4 lines, 16 sides -> 68   DrawCapsule (LineBatchComponent.cpp:578-610 [V])
//   circle   8 segments in the local Y-Z plane (the ChaosDD renderer draws RenderCircle with SphereSegments, :133-143 [V])
#pragma once

#include "CoreTypes.h"

#include <vector>

namespace PhysicsTest::DebugDraw
{
	// One endpoint of a unit-mesh segment. X / Y / Z lie on the unit shape; W is the capsule cap sign (+1 top hemisphere and
	// top circle, -1 bottom, 0 elsewhere): the renderer places a vertex at (X, Y, Z) * Scale + W * HalfAxis * Z.
	struct FWireVertex
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 0.0f;
	};

	inline constexpr int32 GSphereSegments = 8;     // FChaosDDRenderer::SphereSegments
	inline constexpr int32 GCapsuleSides = 16;      // ULineBatchComponent::DrawCapsule DrawCollisionSides
	inline constexpr int32 GCircleSegments = 8;     // FChaosDDRenderer::RenderCircle -> SphereSegments

	// Each appends segment endpoint pairs (2 vertices per segment).
	void AppendUnitBox(std::vector<FWireVertex>& Out);                          // +-1 cube
	void AppendUnitSphere(int32 Segments, std::vector<FWireVertex>& Out);       // unit radius
	void AppendUnitCapsule(int32 Sides, std::vector<FWireVertex>& Out);         // unit radius, caps at W = +-1
	void AppendUnitCircle(int32 Segments, std::vector<FWireVertex>& Out);       // unit radius, Y-Z plane, starts at +Y

	[[nodiscard]] inline int32 CountSegments(const std::vector<FWireVertex>& Vertices) { return static_cast<int32>(Vertices.size() / 2); }
}
