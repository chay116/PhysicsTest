// ViewportCameraController.cpp - FViewportCameraController (see the header). The harness's own code.
#include "ViewportCameraController.h"

#include "Math/Quat.h"
#include "Math/UnrealMathUtility.h"

#include <cmath>

namespace PhysicsTest
{
	namespace
	{
		FVector RotatorForward(const FRotator& Rotation)
		{
			return Rotation.Quaternion().GetAxisX();
		}
		FVector RotatorRight(const FRotator& Rotation)
		{
			return Rotation.Quaternion().GetAxisY();
		}
		FVector RotatorUp(const FRotator& Rotation)
		{
			return Rotation.Quaternion().GetAxisZ();
		}
	}

	void FViewportCameraController::Tick(const FViewportInput& Input, const double DeltaSeconds)
	{
		const double Dt = DeltaSeconds > 0.0 ? FMath::Min(DeltaSeconds, 0.25) : 0.0;
		if (Input.bRightMouse)
		{
			// Fly mode: mouse look, then WASD / EQ acceleration, wheel = speed, C / Z = FOV.
			Rotation.Yaw += Input.MouseDeltaX * MouseLookDegreesPerPixel;
			Rotation.Pitch = FMath::Clamp(Rotation.Pitch - Input.MouseDeltaY * MouseLookDegreesPerPixel, -MaxPitchDegrees, MaxPitchDegrees);
			Rotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw);
			if (Input.WheelNotches != 0.0)
			{
				SpeedScale = FMath::Clamp(SpeedScale * std::pow(WheelSpeedStep, Input.WheelNotches), 0.01, 100.0);
			}
			if (Input.bZoomIn != Input.bZoomOut)
			{
				FOVDegrees = FMath::Clamp(FOVDegrees + (Input.bZoomOut ? 1.0 : -1.0) * FOVDegreesPerSecond * Dt, 5.0, 170.0);
			}
			const FVector Forward = RotatorForward(Rotation);
			const FVector Right = RotatorRight(Rotation);
			FVector Direction;
			Direction += Forward * ((Input.bMoveForward ? 1.0 : 0.0) - (Input.bMoveBackward ? 1.0 : 0.0));
			Direction += Right * ((Input.bMoveRight ? 1.0 : 0.0) - (Input.bMoveLeft ? 1.0 : 0.0));
			Direction += FVector(0.0, 0.0, 1.0) * ((Input.bMoveUp ? 1.0 : 0.0) - (Input.bMoveDown ? 1.0 : 0.0));
			const double MaxSpeed = GetMaxSpeed();
			if (!Direction.IsNearlyZero())
			{
				Velocity += Direction.GetSafeNormal() * (AccelerationRate * SpeedScale * Dt);
				if (Velocity.Size() > MaxSpeed)
				{
					Velocity = Velocity.GetSafeNormal() * MaxSpeed;
				}
			}
			else
			{
				Velocity *= FMath::Max(0.0, 1.0 - DampingPerSecond * Dt);
			}
		}
		else
		{
			Velocity *= FMath::Max(0.0, 1.0 - DampingPerSecond * Dt);
			const FVector Forward = RotatorForward(Rotation);
			if (Input.bAlt && Input.bLeftMouse)
			{
				// Orbit around the pivot at a constant distance [R].
				const double Distance = (Location - OrbitPivot).Size();
				Rotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + Input.MouseDeltaX * MouseLookDegreesPerPixel);
				Rotation.Pitch = FMath::Clamp(Rotation.Pitch - Input.MouseDeltaY * MouseLookDegreesPerPixel, -MaxPitchDegrees, MaxPitchDegrees);
				Location = OrbitPivot - RotatorForward(Rotation) * Distance;
			}
			else if (Input.bLeftMouse)
			{
				// Perspective left drag: vertical = move on the ground plane along the yaw, horizontal = yaw [R].
				const FVector Flat = FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal();
				Location -= Flat * (Input.MouseDeltaY * 2.0 * SpeedScale);
				Rotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + Input.MouseDeltaX * MouseLookDegreesPerPixel);
			}
			else if (Input.bMiddleMouse)
			{
				// Pan in the view plane [R].
				Location += RotatorRight(Rotation) * (-Input.MouseDeltaX * 2.0 * SpeedScale) + RotatorUp(Rotation) * (Input.MouseDeltaY * 2.0 * SpeedScale);
			}
			if (Input.WheelNotches != 0.0)
			{
				Location += Forward * (Input.WheelNotches * DollyCentimetresPerNotch * SpeedScale);
			}
		}
		Location += Velocity * Dt;
	}

	void FViewportCameraController::FocusOn(const FBox& Bounds, const int32 ViewWidth, const int32 ViewHeight)
	{
		if (!Bounds.IsValid)
		{
			return;
		}
		const FSceneView View = MakeView(ViewWidth, ViewHeight);
		const double Radius = FMath::Max(Bounds.GetExtent().Size(), 1.0);
		// The smaller of the two half-FOV tangents decides (1 / ProjScale is tan(half FOV) per axis).
		const double TanHalf = FMath::Min(1.0 / View.ProjScaleX, 1.0 / View.ProjScaleY);
		const double Distance = Radius / std::sin(std::atan(TanHalf));
		OrbitPivot = Bounds.GetCenter();
		Location = OrbitPivot - RotatorForward(Rotation) * Distance;
		Velocity = FVector::ZeroVector;
	}

	void FViewportCameraController::LookAt(const FVector& InLocation, const FVector& Target)
	{
		Location = InLocation;
		const FVector Direction = (Target - InLocation).GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return;
		}
		Rotation.Yaw = FMath::RadiansToDegrees(std::atan2(Direction.Y, Direction.X));
		Rotation.Pitch = FMath::Clamp(FMath::RadiansToDegrees(std::asin(FMath::Clamp(Direction.Z, -1.0, 1.0))), -MaxPitchDegrees, MaxPitchDegrees);
		Rotation.Roll = 0.0;
		OrbitPivot = Target;
	}

	FSceneView FViewportCameraController::MakeView(const int32 ViewWidth, const int32 ViewHeight) const
	{
		FSceneView View;
		View.ViewOrigin = Location;
		View.ViewRotation = Rotation;
		View.FOVDegrees = FOVDegrees;
		View.NearClipPlane = NearClipPlane;
		View.Width = ViewWidth;
		View.Height = ViewHeight;
		View.Update();
		return View;
	}
}
