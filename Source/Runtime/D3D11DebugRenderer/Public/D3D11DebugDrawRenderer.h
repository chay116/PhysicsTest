// D3D11DebugDrawRenderer.h - FD3D11DebugDrawRenderer, the IDebugDrawRenderer of the viewer (02_Architecture 11.1, A.11). [MVP M4]
// The UE line batcher look on D3D11, from the verified prototype (prototypes/render_d3d11/src/proto_render.cpp):
//   - box / sphere / capsule / circle are instances of the unit wire meshes (WireMeshes.h: 12 / 128 / 68 / 8 segments);
//     lines, arrows (shaft + two head lines of length sqrt(ArrowSize), ULineBatchComponent::DrawDirectionalArrow,
//     LineBatchComponent.cpp:361-383 [V]) and triangles are free segments;
//   - every segment is a quad expanded in the vertex shader: the width is the world-space LineThickness (cm) at the
//     segment's depth, at least 1 px (thickness 0 = 1 px);
//   - points are screen-space squares of PointSize px;
//   - pass order (02 11.1): grid (100 cm cells, major every 10, +-5000 cm, depth tested), then the debug lines - with
//     SeeThrough first with depth func Always, then again with NearOrEqual (reversed-Z: GreaterEqual), without it once
//     with NearOrEqual - then the points. Strings are returned to the caller (ImGui text, 02 11.1 step 5).
//   - positions are uploaded camera-relative in float (FSceneView::ViewProjRelative).
// The capsule follows ULineBatchComponent::DrawCapsule: the caps sit at +-max(HalfHeight - Radius, 1) (LineBatchComponent.cpp:588 [V]).
#pragma once

#include "CoreTypes.h"
#include "DebugDrawRenderer.h"
#include "SceneView.h"

#include <memory>
#include <string>
#include <vector>

namespace PhysicsTest
{
	class FD3D11RHI;

	struct FDebugDrawString
	{
		FVector Location;
		std::string Text;
		FColor Color;
		float FontScale = 1.0f;
		bool bDrawShadow = false;
	};

	struct FD3D11DebugDrawStats
	{
		int32 NumBoxes = 0;
		int32 NumSpheres = 0;
		int32 NumCapsules = 0;
		int32 NumCircles = 0;
		int32 NumLines = 0;      // free segments (lines, arrow parts, triangle edges), grid excluded
		int32 NumPoints = 0;
		int32 NumStrings = 0;
		int64 NumSegments = 0;   // every segment drawn per pass (instances x mesh segments + free segments)
		int64 UploadBytes = 0;
	};

	class FD3D11DebugDrawRenderer final : public IDebugDrawRenderer
	{
	public:
		explicit FD3D11DebugDrawRenderer(FD3D11RHI& InRHI);
		~FD3D11DebugDrawRenderer() override;
		FD3D11DebugDrawRenderer(const FD3D11DebugDrawRenderer&) = delete;
		FD3D11DebugDrawRenderer& operator=(const FD3D11DebugDrawRenderer&) = delete;

		// Compiles the shaders (d3dcompiler_47, runtime) and creates the unit-mesh buffer.
		bool Init(std::string& OutError);
		// Starts a frame: the view and the draw region (IDebugDrawRenderer::GetDrawRegion); clears the batches.
		void BeginView(const FSceneView& View, const FSphere& DrawRegion);
		void DrawGrid(double Cell = 100.0, int32 MajorEvery = 10, double HalfExtent = 5000.0);
		// Uploads the batches and draws them into the bound scene targets (FD3D11RHI::BeginScene).
		void Flush(bool bSeeThrough);

		[[nodiscard]] const std::vector<FDebugDrawString>& GetStrings() const { return Strings; }
		[[nodiscard]] const FD3D11DebugDrawStats& GetStats() const { return Stats; }
		[[nodiscard]] const FSceneView& GetView() const { return View; }

		// IDebugDrawRenderer
		[[nodiscard]] bool IsServer() const override { return false; }
		[[nodiscard]] FSphere GetDrawRegion() const override { return DrawRegion; }
		void RenderPoint(const FVector& Position, const FColor& Color, float PointSize, float Lifetime) override;
		void RenderLine(const FVector& A, const FVector& B, const FColor& Color, float LineThickness, float Lifetime) override;
		void RenderArrow(const FVector& A, const FVector& B, float ArrowSize, const FColor& Color, float LineThickness, float Lifetime) override;
		void RenderCircle(const FVector& Center, const FMatrix& Axes, float Radius, const FColor& Color, float LineThickness, float Lifetime) override;
		void RenderSphere(const FVector& Center, float Radius, const FColor& Color, float LineThickness, float Lifetime) override;
		void RenderCapsule(const FVector& Center, const FQuat& Rotation, float HalfHeight, float Radius, const FColor& Color, float LineThickness,
			float Lifetime) override;
		void RenderBox(const FVector& Position, const FQuat& Rotation, const FVector& Size, const FColor& Color, float LineThickness, float Lifetime) override;
		void RenderTriangle(const FVector& A, const FVector& B, const FVector& C, const FColor& Color, float LineThickness, float Lifetime) override;
		void RenderString(const FVector& TextLocation, std::string_view Text, const FColor& Color, float FontScale, bool bDrawShadow, float Lifetime) override;

		// GPU instance layout (80 B): rotation * scale rows with the camera-relative translation in w, then
		// (scale x, scale y, scale z, capsule cap centre offset), colour (R in the low byte) and thickness (cm).
		struct FGpuInstance
		{
			float R0[4];
			float R1[4];
			float R2[4];
			float Params[4];
			uint32 Color;
			float Thickness;
			uint32 Pad[2];
		};
		struct FGpuLine
		{
			float A[3];
			uint32 Color;
			float B[3];
			float Thickness;
		};
		struct FGpuPoint
		{
			float P[3];
			uint32 Color;
			float Size;
			float Pad[3];
		};
		enum EMesh : int32 { Mesh_Box, Mesh_Sphere, Mesh_Capsule, Mesh_Circle, Mesh_Count };

	private:
		void AddInstance(EMesh Mesh, const FVector& Center, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ, const FVector& Scale, double CapCentreOffset,
			const FColor& Color, float Thickness);
		void AddLine(std::vector<FGpuLine>& Target, const FVector& A, const FVector& B, const FColor& Color, float Thickness);

		struct FImpl;
		std::unique_ptr<FImpl> Impl;
		FD3D11RHI& RHI;
		FSceneView View;
		FSphere DrawRegion;
		std::vector<FGpuInstance> Instances[Mesh_Count];
		std::vector<FGpuLine> Lines;
		std::vector<FGpuLine> GridLines;
		std::vector<FGpuPoint> Points;
		std::vector<FDebugDrawString> Strings;
		FD3D11DebugDrawStats Stats;
	};
}
