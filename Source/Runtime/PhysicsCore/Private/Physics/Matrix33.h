// Matrix33.h - PRIVATE 3x3 helpers for mass properties and reference frames (column-vector convention:
// M * v, a rotation matrix's columns are the images of the unit axes). Not a UE type; UE's FMatrix33 (Chaos) and
// FMatrix (row-vector convention) are not mirrored here. Core gets an FMatrix in M4 (renderer).
#pragma once

#include "Math/Quat.h"
#include "Math/Vector.h"

namespace PhysicsTest::Private
{
	struct FMatrix33
	{
		double M[3][3] = {};   // M[Row][Column]

		[[nodiscard]] static FMatrix33 Identity();
		[[nodiscard]] static FMatrix33 Diagonal(const FVector& D);
		// Rotation matrix of a unit quaternion (columns = Q * X, Q * Y, Q * Z).
		[[nodiscard]] static FMatrix33 FromQuat(const FQuat& Q);
		// Matrix with the given columns.
		[[nodiscard]] static FMatrix33 FromColumns(const FVector& C0, const FVector& C1, const FVector& C2);

		[[nodiscard]] FMatrix33 operator*(const FMatrix33& B) const;
		[[nodiscard]] FVector operator*(const FVector& V) const;
		[[nodiscard]] FMatrix33 GetTransposed() const;
		[[nodiscard]] FVector GetColumn(int32 Column) const { return FVector(M[0][Column], M[1][Column], M[2][Column]); }
		[[nodiscard]] FVector GetDiagonal() const { return FVector(M[0][0], M[1][1], M[2][2]); }
		[[nodiscard]] double Determinant() const;
	};

	// Unit quaternion of a proper rotation matrix (det +1); Shepperd's method.
	[[nodiscard]] FQuat QuatFromRotationMatrix(const FMatrix33& R);

	// R * diag(D) * R^T (Chaos::Utilities::ComputeWorldSpaceInertia, Utilities.h:327-337 [V]).
	[[nodiscard]] FMatrix33 ComputeWorldSpaceInertia(const FQuat& Rotation, const FVector& Diagonal);

	// Eigen-decomposition of a symmetric matrix by cyclic Jacobi rotations: A = V * diag(OutEigenvalues) * V^T with
	// orthonormal V, det(V) = +1. Returns V.
	[[nodiscard]] FMatrix33 DiagonalizeSymmetric(const FMatrix33& A, FVector& OutEigenvalues);
}
