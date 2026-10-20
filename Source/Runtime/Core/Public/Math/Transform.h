// Transform.h - FTransform (rotation, translation, scale; double). UE: Core/Public/Math/TransformNonVectorized.h.
// Composition order is UE's: C = A * B applies A first, then B
//   C.Rotation = B.Rotation * A.Rotation, C.Scale3D = A.Scale3D * B.Scale3D,
//   C.Translation = B.Rotation.RotateVector(B.Scale3D * A.Translation) + B.Translation
// (TransformNonVectorized.h:1308-1342). UE's negative-scale path (MultiplyUsingMatrixWithScale) is not mirrored:
// composing transforms with negative scale is rejected by check().
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

		// Exact for uniform scale; for non-uniform scale it follows UE's component-wise definition.
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
