// PhysXDebugRender.h - native debug draw of the PhysX backend (M9, docs/Abi.md 9.2 native draw, final in ABI v1.1): PhysX's own
// debug visualization (PxScene::getRenderBuffer) as PtDebugDrawCmd records. [확장 M9]   PT_BACKEND_NS=PhysX
//
// Viz runs only (PtSceneDesc::RunMode == PT_RUN_VIZ): InitPhysScene sets PxVisualizationParameter::eSCALE and the variant's
// DebugVisualization categories; bench and behaviour runs never set a visualization parameter, so eSCALE stays 0 and
// PhysX skips its visualization entirely (NpDebugViz.cpp:1020-1022, ScVisualize.cpp:170-176 [V]).
//
// What the buffer holds after a step [V]: NpScene::simulate calls visualize() before the step runs (NpScene.cpp:2927-2928),
// which draws the shapes, actor / body axes and the joint frames (Sc::Scene::visualizeStartStep, ScVisualize.cpp:152-211)
// at the pose the step STARTED from; the contacts are drawn at the step's end (Sc::Scene::visualizeContacts,
// ScVisualize.cpp:213-269, ScPipeline.cpp:2817) from that step's narrow phase - the start-of-step pose too, as the snapshot's
// contact points (contact.point_pose 0). So the native commands of a snapshot lag its particles by one step.
//
// Conversion (PxRenderBuffer.h [V]): a PxDebugPoint becomes PT_DD_POINT, a PxDebugLine PT_DD_LINE, a PxDebugTriangle
// PT_DD_TRIANGLE; positions are converted to cm with the variant's LengthScale. PhysX colours are packed ARGB
// (PxDebugColor::eARGB_*), which is FColor's packed layout (PtDebugDrawCmd::ColorBGRA, read with FColor::FromPackedARGB by the
// host), so they pass through unchanged. A line or triangle carries one colour per vertex in PhysX and one per command in
// the ABI: the first vertex's colour is kept and the others are counted (NumMultiColour). PhysX 5.11's render buffer has
// no text (no PxDebugText accessor), so no PT_DD_STRING and no string bytes.
#pragma once

#include "PhysXIncludes.h"
#include "PhysicsTestAbi/PtBackendKit.h"
#include "PtTypes.h"
#include "PtUnitScale.h"

namespace PhysicsTest::PhysXBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	// The harness's visualization lengths (cm), independent of the engine's units.
	inline constexpr double GVizAxisLengthCm = 20.0;      // actor axes, joint local frames, joint limits
	inline constexpr double GVizNormalLengthCm = 10.0;    // contact normals
	inline constexpr double GVizVelocitySeconds = 0.1;    // a linear velocity arrow is the distance covered in this time
	inline constexpr float GVizPointSizePx = 4.0f;        // PT_DD_POINT size (PhysX points carry no size)

	// eSCALE = "1 m" in native units (100 x LengthScale): PhysX draws its contact-point crosses 0.1 x eSCALE wide
	// (ScVisualize.cpp:99-111 [V]), so they are 10 cm in both unit systems. Then every category of Categories
	// (EPhysXVisualizationCategory bits) with the lengths above divided by eSCALE (PhysX multiplies each parameter by eSCALE).
	void ApplyVisualizationParameters(physx::PxScene& Scene, uint32 Categories, const ::PhysicsTest::Abi::FUnitScale& Scale);

	struct FPhysXRenderCounts
	{
		uint32 NumPoints = 0;
		uint32 NumLines = 0;
		uint32 NumTriangles = 0;
		uint32 NumMultiColour = 0;   // lines / triangles whose vertices had different colours (the first one was kept)
		uint32 Total() const { return NumPoints + NumLines + NumTriangles; }
	};

	// What the buffer holds now.
	FPhysXRenderCounts CountRenderBuffer(const physx::PxRenderBuffer& Buffer);

	// Appends every primitive of Buffer (points, then lines, then triangles, each in buffer order) to Out as PtDebugDrawCmd
	// records, at most MaxCommands in all; bTruncated is set when the buffer held more. Out is not cleared.
	FPhysXRenderCounts ConvertRenderBuffer(const physx::PxRenderBuffer& Buffer, const ::PhysicsTest::Abi::FUnitScale& Scale, uint32 MaxCommands,
		physx::PxArray<PtDebugDrawCmd>& Out, bool& bTruncated);
}
