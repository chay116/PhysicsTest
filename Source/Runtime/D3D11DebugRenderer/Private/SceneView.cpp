// SceneView.cpp - FSceneView (see SceneView.h). The harness's own code.
#include "SceneView.h"

#include "Math/UnrealMathUtility.h"

#include <cmath>

namespace PhysicsTest
{
	void FSceneView::Update()
	{
		const FQuat Q = ViewRotation.Quaternion();
		Forward = Q.GetAxisX();
		Right = Q.GetAxisY();
		Up = Q.GetAxisZ();
		const double SafeWidth = Width > 0 ? static_cast<double>(Width) : 1.0;
		const double SafeHeight = Height > 0 ? static_cast<double>(Height) : 1.0;
		const double HalfFov = FMath::DegreesToRadians(FMath::Clamp(FOVDegrees, 1.0, 179.0)) * 0.5;
		const double MajorScale = 1.0 / std::tan(HalfFov);
		if (SafeWidth >= SafeHeight)
		{
			ProjScaleX = MajorScale;
			ProjScaleY = MajorScale * SafeWidth / SafeHeight;
		}
		else
		{
			ProjScaleY = MajorScale;
			ProjScaleX = MajorScale * SafeHeight / SafeWidth;
		}
		// Rows of the view rotation (world -> view: x = Right, y = Up, z = Forward), then the projection:
		//   clip.x = ProjScaleX * x, clip.y = ProjScaleY * y, clip.z = Near, clip.w = z.
		const double Rows[3][3] = { { Right.X, Right.Y, Right.Z }, { Up.X, Up.Y, Up.Z }, { Forward.X, Forward.Y, Forward.Z } };
		for (int32 Column = 0; Column < 3; ++Column)
		{
			ViewProjRelative[0][Column] = static_cast<float>(ProjScaleX * Rows[0][Column]);
			ViewProjRelative[1][Column] = static_cast<float>(ProjScaleY * Rows[1][Column]);
			ViewProjRelative[2][Column] = 0.0f;
			ViewProjRelative[3][Column] = static_cast<float>(Rows[2][Column]);
		}
		ViewProjRelative[0][3] = 0.0f;
		ViewProjRelative[1][3] = 0.0f;
		ViewProjRelative[2][3] = static_cast<float>(NearClipPlane);
		ViewProjRelative[3][3] = 0.0f;
	}

	FVector FSceneView::WorldToView(const FVector& World) const
	{
		const FVector Delta = World - ViewOrigin;
		return FVector(Delta | Right, Delta | Up, Delta | Forward);
	}

	bool FSceneView::ProjectWorldToPixel(const FVector& World, double& OutX, double& OutY) const
	{
		const FVector View = WorldToView(World);
		if (View.Z < NearClipPlane)
		{
			return false;
		}
		const double NdcX = ProjScaleX * View.X / View.Z;
		const double NdcY = ProjScaleY * View.Y / View.Z;
		OutX = (NdcX * 0.5 + 0.5) * static_cast<double>(Width);
		OutY = (0.5 - NdcY * 0.5) * static_cast<double>(Height);
		return true;
	}

	void FSceneView::DeprojectPixel(const double PixelX, const double PixelY, FVector& OutOrigin, FVector& OutDirection) const
	{
		const double NdcX = (PixelX / static_cast<double>(Width > 0 ? Width : 1)) * 2.0 - 1.0;
		const double NdcY = 1.0 - (PixelY / static_cast<double>(Height > 0 ? Height : 1)) * 2.0;
		const FVector ViewDirection(NdcX / ProjScaleX, NdcY / ProjScaleY, 1.0);
		OutOrigin = ViewOrigin;
		OutDirection = (Right * ViewDirection.X + Up * ViewDirection.Y + Forward * ViewDirection.Z).GetSafeNormal();
	}

	double FSceneView::GetPixelsPerUnitAtDepth(const double ViewZ) const
	{
		return ViewZ > 0.0 ? ProjScaleX * 0.5 * static_cast<double>(Width) / ViewZ : 0.0;
	}
}
