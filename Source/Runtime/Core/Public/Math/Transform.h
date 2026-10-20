// Transform.h - FTransform (rotation, translation, scale; double). UE reference: on Win64 UE compiles the vectorized
// implementation (ENABLE_VECTORIZED_TRANSFORM = PLATFORM_ENABLE_VECTORINTRINSICS, ScalarRegister.h:15-16), i.e.
// Core/Public/Math/TransformVectorized.h and Core/Private/Math/TransformVectorized.cpp. The scalar
// TransformNonVectorized.h computes the same values except Inverse() of an all-zero scale (see Inverse below).
// Composition order is UE's: C = A * B applies A first, then B
//   C.Rotation = B.Rotation * A.Rotation, C.Scale3D = A.Scale3D * B.Scale3D,
//   C.Translation = B.Rotation.RotateVector(B.Scale3D * A.Translation) + B.Translation
// (TransformVectorized.cpp:295-345). UE composes transforms with a negative scale component through matrices
// (MultiplyUsingMatrixWithScale); the harness does not mirror that path and stops with a Fatal log in EVERY build
// (exit code 3, CTest PhysicsTest.Probe.NegativeScaleFatal).
// Default construction is the identity, as in UE (TTransform()). Unlike UE, FVector / FQuat default to zero /
// identity instead of being left uninitialised (02_Architecture 5.2).
#pragma once

#include "Math/Quat.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

namespace PhysicsTest
{
	struct FTransform
	{
		FQuat Rotation;
		FVector Translation;
		FVector Scale3D = FVector(1.0);

		static const FTransform Identity;

		FTransform() = default;
		explicit FTransform(const FVector& InTranslation) : Translation(InTranslation) {}
		explicit FTransform(const FQuat& InRotation) : Rotation(InRotation) {}
		explicit FTransform(const FRotator& InRotation) : Rotation(InRotation.Quaternion()) {}
		FTransform(const FQuat& InRotation, const FVector& InTranslation, const FVector& InScale3D = FVector(1.0))
			: Rotation(InRotation), Translation(InTranslation), Scale3D(InScale3D) {}
		FTransform(const FRotator& InRotation, const FVector& InTranslation, const FVector& InScale3D = FVector(1.0))
			: Rotation(InRotation.Quaternion()), Translation(InTranslation), Scale3D(InScale3D) {}

		// A * B: A first, then B (UE order).
		[[nodiscard]] FTransform operator*(const FTransform& Other) const;
		FTransform& operator*=(const FTransform& Other);
		static void Multiply(FTransform* OutTransform, const FTransform* A, const FTransform* B);

		[[nodiscard]] FVector TransformPosition(const FVector& V) const;
		[[nodiscard]] FVector TransformPositionNoScale(const FVector& V) const;
		[[nodiscard]] FVector TransformVector(const FVector& V) const;
		[[nodiscard]] FVector TransformVectorNoScale(const FVector& V) const;
		[[nodiscard]] FVector InverseTransformPosition(const FVector& V) const;
		[[nodiscard]] FVector InverseTransformPositionNoScale(const FVector& V) const;
		[[nodiscard]] FVector InverseTransformVector(const FVector& V) const;
		[[nodiscard]] FVector InverseTransformVectorNoScale(const FVector& V) const;
		[[nodiscard]] FQuat TransformRotation(const FQuat& Q) const { return Rotation * Q; }
		[[nodiscard]] FQuat InverseTransformRotation(const FQuat& Q) const { return Rotation.Inverse() * Q; }

		// Exact for uniform scale; for non-uniform scale it follows UE's component-wise definition. When no |Scale3D|
		// component exceeds UE_SMALL_NUMBER the result is Identity (TransformVectorized.h:363-374); otherwise scale
		// components within UE_SMALL_NUMBER of zero invert to zero (GetSafeScaleReciprocal).
		[[nodiscard]] FTransform Inverse() const;

		[[nodiscard]] FVector GetLocation() const { return Translation; }
		[[nodiscard]] const FVector& GetTranslation() const { return Translation; }
		[[nodiscard]] const FQuat& GetRotation() const { return Rotation; }
		[[nodiscard]] const FVector& GetScale3D() const { return Scale3D; }
		[[nodiscard]] FRotator Rotator() const { return Rotation.Rotator(); }
		void SetLocation(const FVector& V) { Translation = V; }
		void SetTranslation(const FVector& V) { Translation = V; }
		void SetRotation(const FQuat& Q) { Rotation = Q; }
		void SetScale3D(const FVector& S) { Scale3D = S; }

		[[nodiscard]] bool IsRotationNormalized() const { return Rotation.IsNormalized(); }
		[[nodiscard]] bool Equals(const FTransform& Other, double Tolerance = UE_KINDA_SMALL_NUMBER) const;
		[[nodiscard]] bool ContainsNaN() const;

		// 1 / Scale per component; components within Tolerance of zero map to zero (UE: GetSafeScaleReciprocal).
		[[nodiscard]] static FVector GetSafeScaleReciprocal(const FVector& InScale, double Tolerance = UE_SMALL_NUMBER);
	};

	inline const FTransform FTransform::Identity;
}
