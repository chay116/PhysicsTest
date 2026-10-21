// Matrix.cpp - FMatrix (see Math/Matrix.h). The harness's own code; semantics follow UE's row-vector FMatrix.
#include "Math/Matrix.h"

#include <cmath>

namespace PhysicsTest
{
	FVector FMatrix::GetScaledAxis(const EAxis Axis) const
	{
		switch (Axis)
		{
		case EAxis::X: return FVector(M[0][0], M[0][1], M[0][2]);
		case EAxis::Y: return FVector(M[1][0], M[1][1], M[1][2]);
		case EAxis::Z: return FVector(M[2][0], M[2][1], M[2][2]);
		default: return FVector::ZeroVector;
		}
	}

	void FMatrix::SetAxis(const int32 Index, const FVector& Axis)
	{
		M[Index][0] = Axis.X;
		M[Index][1] = Axis.Y;
		M[Index][2] = Axis.Z;
	}

	void FMatrix::SetOrigin(const FVector& Origin)
	{
		M[3][0] = Origin.X;
		M[3][1] = Origin.Y;
		M[3][2] = Origin.Z;
	}

	FVector FMatrix::TransformPosition(const FVector& V) const
	{
		return TransformVector(V) + GetOrigin();
	}

	FVector FMatrix::TransformVector(const FVector& V) const
	{
		return FVector(V.X * M[0][0] + V.Y * M[1][0] + V.Z * M[2][0], V.X * M[0][1] + V.Y * M[1][1] + V.Z * M[2][1],
			V.X * M[0][2] + V.Y * M[1][2] + V.Z * M[2][2]);
	}

	FQuat FMatrix::ToQuat() const
	{
		// Orthonormal rotation part (row i = axis i), then the largest-component branch for stability.
		const FVector Axes[3] = { GetUnitAxis(EAxis::X), GetUnitAxis(EAxis::Y), GetUnitAxis(EAxis::Z) };
		double R[3][3];
		for (int32 Row = 0; Row < 3; ++Row)
		{
			R[Row][0] = Axes[Row].X;
			R[Row][1] = Axes[Row].Y;
			R[Row][2] = Axes[Row].Z;
		}
		// Row-vector matrices: R[i][j] is the j component of axis i, i.e. the transpose of the column-vector rotation.
		const double Trace = R[0][0] + R[1][1] + R[2][2];
		FQuat Q;
		if (Trace > 0.0)
		{
			const double S = std::sqrt(Trace + 1.0) * 2.0;
			Q.W = 0.25 * S;
			Q.X = (R[1][2] - R[2][1]) / S;
			Q.Y = (R[2][0] - R[0][2]) / S;
			Q.Z = (R[0][1] - R[1][0]) / S;
		}
		else if (R[0][0] > R[1][1] && R[0][0] > R[2][2])
		{
			const double S = std::sqrt(1.0 + R[0][0] - R[1][1] - R[2][2]) * 2.0;
			Q.W = (R[1][2] - R[2][1]) / S;
			Q.X = 0.25 * S;
			Q.Y = (R[1][0] + R[0][1]) / S;
			Q.Z = (R[2][0] + R[0][2]) / S;
		}
		else if (R[1][1] > R[2][2])
		{
			const double S = std::sqrt(1.0 + R[1][1] - R[0][0] - R[2][2]) * 2.0;
			Q.W = (R[2][0] - R[0][2]) / S;
			Q.X = (R[1][0] + R[0][1]) / S;
			Q.Y = 0.25 * S;
			Q.Z = (R[2][1] + R[1][2]) / S;
		}
		else
		{
			const double S = std::sqrt(1.0 + R[2][2] - R[0][0] - R[1][1]) * 2.0;
			Q.W = (R[0][1] - R[1][0]) / S;
			Q.X = (R[2][0] + R[0][2]) / S;
			Q.Y = (R[2][1] + R[1][2]) / S;
			Q.Z = 0.25 * S;
		}
		return Q.GetNormalized();
	}

	FMatrix FMatrix::FromQuat(const FQuat& Q, const FVector& Origin)
	{
		return FMatrix(Q.GetAxisX(), Q.GetAxisY(), Q.GetAxisZ(), Origin);
	}

	namespace
	{
		// The reference direction of the one-axis bases: world up, or world X when the given axis is (nearly) vertical. Same
		// rule and threshold as UE's FRotationMatrix::MakeFromX / MakeFromZ (RotationMatrix.h:76-115 [V]), so the
		// harness's contact discs and sleeping boxes get UE's in-plane orientation.
		FVector GetReferenceDirection(const FVector& Axis)
		{
			return FMath::Abs(Axis.Z) < (1.0 - UE_KINDA_SMALL_NUMBER) ? FVector(0.0, 0.0, 1.0) : FVector(1.0, 0.0, 0.0);
		}
	}

	FMatrix FMatrix::MakeFromX(const FVector& XAxis)
	{
		// X given; Y = reference x X (normalised); Z completes the right-handed basis.
		const FVector Forward = XAxis.GetSafeNormal();
		const FVector Side = (GetReferenceDirection(Forward) ^ Forward).GetSafeNormal();
		return FMatrix(Forward, Side, Forward ^ Side, FVector::ZeroVector);
	}

	FMatrix FMatrix::MakeFromZ(const FVector& ZAxis)
	{
		// Z given; X = reference x Z (normalised); Y completes the right-handed basis.
		const FVector Normal = ZAxis.GetSafeNormal();
		const FVector Forward = (GetReferenceDirection(Normal) ^ Normal).GetSafeNormal();
		return FMatrix(Forward, Normal ^ Forward, Normal, FVector::ZeroVector);
	}
}
