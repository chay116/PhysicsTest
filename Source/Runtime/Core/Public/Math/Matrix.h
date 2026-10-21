// Matrix.h - FMatrix (4x4 double) and EAxis, minimal. UE: Core/Public/Math/Matrix.h, RotationMatrix.h, Axis.h.
// [MVP M4: the debug draw passes axes as a matrix, as Chaos does (IChaosDDRenderer::RenderCircle, ChaosDDRenderer.h:37 [V])]
// UE convention: row vectors. Row 0 / 1 / 2 are the X / Y / Z axes, row 3 the origin; TransformPosition(P) =
// P.X * M[0] + P.Y * M[1] + P.Z * M[2] + M[3]. Only what the harness needs is mirrored: axis access, quaternion
// conversion, and FRotationMatrix::MakeFromX / MakeFromZ (RotationMatrix.h:76-115 [V]).
#pragma once

#include "Math/Quat.h"
#include "Math/Vector.h"

namespace PhysicsTest
{
	// UE: EAxis::Type (Math/Axis.h).
	enum class EAxis : uint8
	{
		None,
		X,
		Y,
		Z,
	};

	struct FMatrix
	{
		double M[4][4] = { { 1.0, 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0, 0.0 }, { 0.0, 0.0, 0.0, 1.0 } };

		static const FMatrix Identity;

		constexpr FMatrix() = default;
		// UE FMatrix(InX, InY, InZ, InW): the three axes and the origin as rows (w = 0, 0, 0, 1).
		constexpr FMatrix(const FVector& InX, const FVector& InY, const FVector& InZ, const FVector& InW)
			: M{ { InX.X, InX.Y, InX.Z, 0.0 }, { InY.X, InY.Y, InY.Z, 0.0 }, { InZ.X, InZ.Y, InZ.Z, 0.0 }, { InW.X, InW.Y, InW.Z, 1.0 } }
		{
		}

		[[nodiscard]] FVector GetScaledAxis(EAxis Axis) const;
		[[nodiscard]] FVector GetUnitAxis(EAxis Axis) const { return GetScaledAxis(Axis).GetSafeNormal(); }
		void SetAxis(int32 Index, const FVector& Axis);
		[[nodiscard]] FVector GetOrigin() const { return FVector(M[3][0], M[3][1], M[3][2]); }
		void SetOrigin(const FVector& Origin);
		[[nodiscard]] FVector TransformPosition(const FVector& V) const;
		[[nodiscard]] FVector TransformVector(const FVector& V) const;
		// The rotation of the (orthonormalised) axes (UE: FMatrix::ToQuat, the Shoemake branch form).
		[[nodiscard]] FQuat ToQuat() const;

		// UE FQuatRotationTranslationMatrix(Q, Origin).
		[[nodiscard]] static FMatrix FromQuat(const FQuat& Q, const FVector& Origin = FVector::ZeroVector);
		// UE FRotationMatrix::MakeFromX: X fixed; Y = Up ^ X with Up = +Z (or +X when X is nearly +-Z); Z = X ^ Y.
		[[nodiscard]] static FMatrix MakeFromX(const FVector& XAxis);
		// UE FRotationMatrix::MakeFromZ: Z fixed; X = Up ^ Z with Up = +Z (or +X when Z is nearly +-Z); Y = Z ^ X.
		[[nodiscard]] static FMatrix MakeFromZ(const FVector& ZAxis);
	};

	inline const FMatrix FMatrix::Identity;
}
