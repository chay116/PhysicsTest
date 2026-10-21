// ViewportCameraController.h - FViewportCameraController, the UE-editor-style perspective camera of the viewer
// (02_Architecture 11.2, 03 M4). [MVP M4] Pure math: input in, camera out, so the controls are unit tests
// (PhysicsTest.Viewer.Camera*), not synthetic input.
// Conventions (UE editor viewport, FEditorViewportClient / FEditorCameraController; values marked [R] are the harness's
// choice where the design gives none):
//   - hFOV 90 on the major axis, near plane 10 cm (FSceneView);
//   - right mouse button held = fly mode: mouse look (yaw / pitch), W/S forward/back and A/D left/right along the camera,
//     E/Q straight up/down in world Z, C/Z zoom the FOV in/out, the wheel scales the fly speed by +-10 % per notch;
//   - fly movement accelerates at 20000 cm/s^2 (x speed scale) up to the maximum speed and damps at 10 / s when no key
//     is held (02 11.2);
//   - without the right button: left drag moves forward/back on the ground plane and yaws, middle drag pans, the wheel
//     dollies along the view direction [R];
//   - F frames the selection (its bounds fill the view), Alt + left drag orbits around the pivot [R].
#pragma once

#include "CoreTypes.h"
#include "Math/Box.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"
#include "SceneView.h"

namespace PhysicsTest
{
	struct FViewportInput
	{
		bool bRightMouse = false;
		bool bLeftMouse = false;
		bool bMiddleMouse = false;
		bool bAlt = false;
		double MouseDeltaX = 0.0;   // pixels this frame (+ right)
		double MouseDeltaY = 0.0;   // pixels this frame (+ down)
		double WheelNotches = 0.0;  // + away from the user
		bool bMoveForward = false;  // W
		bool bMoveBackward = false; // S
		bool bMoveLeft = false;     // A
		bool bMoveRight = false;    // D
		bool bMoveUp = false;       // E
		bool bMoveDown = false;     // Q
		bool bZoomIn = false;       // C
		bool bZoomOut = false;      // Z
	};

	class FViewportCameraController
	{
	public:
		static constexpr double DefaultFOVDegrees = 90.0;
		static constexpr double NearClipPlane = 10.0;               // cm
		static constexpr double AccelerationRate = 20000.0;         // cm/s^2 (UE FCameraControllerConfig MovementAccelerationRate)
		static constexpr double DampingPerSecond = 10.0;            // 1/s (UE MovementVelocityDampingAmount)
		static constexpr double BaseMaxSpeed = 1000.0;              // cm/s at speed scale 1 [R]
		static constexpr double WheelSpeedStep = 1.1;               // +-10 % per notch in fly mode
		static constexpr double MouseLookDegreesPerPixel = 0.25;    // [R]
		static constexpr double FOVDegreesPerSecond = 60.0;         // C / Z [R]
		static constexpr double DollyCentimetresPerNotch = 100.0;   // wheel without the right button, x speed scale [R]
		static constexpr double MaxPitchDegrees = 89.0;

		FVector Location = FVector(-800.0, 0.0, 300.0);
		FRotator Rotation = FRotator(-15.0, 0.0, 0.0);
		double FOVDegrees = DefaultFOVDegrees;
		double SpeedScale = 1.0;
		FVector Velocity;          // fly-mode velocity, cm/s
		FVector OrbitPivot;        // Alt + left drag pivot (the selection or a point ahead)

		void Tick(const FViewportInput& Input, double DeltaSeconds);
		// F: keeps the rotation and moves back along the view direction until Bounds' bounding sphere fits the smaller FOV.
		void FocusOn(const FBox& Bounds, int32 ViewWidth, int32 ViewHeight);
		// Places the camera at Location looking at Target (no roll).
		void LookAt(const FVector& InLocation, const FVector& Target);
		[[nodiscard]] double GetMaxSpeed() const { return BaseMaxSpeed * SpeedScale; }
		[[nodiscard]] FSceneView MakeView(int32 ViewWidth, int32 ViewHeight) const;
	};
}
