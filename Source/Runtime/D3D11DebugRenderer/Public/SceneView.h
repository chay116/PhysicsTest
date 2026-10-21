// SceneView.h - FSceneView: the camera of one viewport (02_Architecture 11.1, 11.2; A.11). [MVP M4]
// Pure math (no D3D), so the camera conventions are unit tests. UE conventions:
//   - world: X forward, Y right, Z up, cm (left-handed);
//   - view basis swap (UE FViewMatrices: view X = camera right, view Y = camera up, view Z = camera forward, i.e. the
//     inverse view rotation followed by the (0,0,1)(1,0,0)(0,1,0) swap), which is D3D's left-handed view space;
//   - FOV is horizontal and fixed on the major axis: for a landscape viewport the X FOV is FOVDegrees (UE editor default
//     AspectRatio_MaintainXFOV); for a portrait one the Y FOV is;
//   - reversed-Z with an infinite far plane: depth = NearClipPlane / view z (1 at the near plane, -> 0 at infinity), as UE's
//     FReversedZPerspectiveMatrix with far = near.
// The GPU matrix is camera-relative (positions are uploaded as world - ViewOrigin in float), so a 200 m ground box does not
// lose float precision at the camera.
#pragma once

#include "CoreTypes.h"
#include "Math/Quat.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

namespace PhysicsTest
{
	struct FSceneView
	{
		FVector ViewOrigin;
		FRotator ViewRotation;
		double FOVDegrees = 90.0;      // on the major axis
		double NearClipPlane = 10.0;   // cm
		int32 Width = 1600;
		int32 Height = 900;

		// Derived by Update().
		FVector Forward = FVector(1.0, 0.0, 0.0);
		FVector Right = FVector(0.0, 1.0, 0.0);
		FVector Up = FVector(0.0, 0.0, 1.0);
		double ProjScaleX = 1.0;       // clip x = ProjScaleX * view x (1 / tan(hFOV / 2) for a landscape viewport)
		double ProjScaleY = 1.0;
		// clip = M * (world - ViewOrigin, 1), column-vector convention, row-major storage (HLSL row_major + mul(M, v)).
		float ViewProjRelative[4][4] = {};

		void Update();

		// View space of a world point: x right, y up, z forward (cm).
		[[nodiscard]] FVector WorldToView(const FVector& World) const;
		// Pixel coordinates (origin top-left, y down). False when the point is not in front of the near plane.
		bool ProjectWorldToPixel(const FVector& World, double& OutX, double& OutY) const;
		// The ray through a pixel (origin = ViewOrigin, unit direction).
		void DeprojectPixel(double PixelX, double PixelY, FVector& OutOrigin, FVector& OutDirection) const;
		// Pixels per cm at view depth ViewZ along the screen X axis (world-space line widths).
		[[nodiscard]] double GetPixelsPerUnitAtDepth(double ViewZ) const;
	};
}
