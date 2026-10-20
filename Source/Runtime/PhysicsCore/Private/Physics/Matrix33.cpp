// Matrix33.cpp - 3x3 helpers (column-vector convention) for BodyUtils and constraint frames.
#include "Physics/Matrix33.h"

#include "Math/UnrealMathUtility.h"

namespace PhysicsTest::Private
{
	FMatrix33 FMatrix33::Identity()
	{
		return Diagonal(FVector(1.0));
	}

	FMatrix33 FMatrix33::Diagonal(const FVector& D)
	{
		FMatrix33 Result;
		Result.M[0][0] = D.X;
		Result.M[1][1] = D.Y;
		Result.M[2][2] = D.Z;
		return Result;
	}

	FMatrix33 FMatrix33::FromQuat(const FQuat& Q)
	{
		const double X2 = Q.X + Q.X, Y2 = Q.Y + Q.Y, Z2 = Q.Z + Q.Z;
		const double XX = Q.X * X2, YY = Q.Y * Y2, ZZ = Q.Z * Z2;
		const double XY = Q.X * Y2, XZ = Q.X * Z2, YZ = Q.Y * Z2;
		const double WX = Q.W * X2, WY = Q.W * Y2, WZ = Q.W * Z2;
		FMatrix33 R;
		R.M[0][0] = 1.0 - (YY + ZZ); R.M[0][1] = XY - WZ;         R.M[0][2] = XZ + WY;
		R.M[1][0] = XY + WZ;         R.M[1][1] = 1.0 - (XX + ZZ); R.M[1][2] = YZ - WX;
		R.M[2][0] = XZ - WY;         R.M[2][1] = YZ + WX;         R.M[2][2] = 1.0 - (XX + YY);
		return R;
	}

	FMatrix33 FMatrix33::FromColumns(const FVector& C0, const FVector& C1, const FVector& C2)
	{
		FMatrix33 R;
		for (int32 Row = 0; Row < 3; ++Row)
		{
			R.M[Row][0] = C0[Row];
			R.M[Row][1] = C1[Row];
			R.M[Row][2] = C2[Row];
		}
		return R;
	}

	FMatrix33 FMatrix33::operator*(const FMatrix33& B) const
	{
		FMatrix33 Result;
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Column = 0; Column < 3; ++Column)
			{
				Result.M[Row][Column] = M[Row][0] * B.M[0][Column] + M[Row][1] * B.M[1][Column] + M[Row][2] * B.M[2][Column];
			}
		}
		return Result;
	}

	FVector FMatrix33::operator*(const FVector& V) const
	{
		return FVector(
			M[0][0] * V.X + M[0][1] * V.Y + M[0][2] * V.Z,
			M[1][0] * V.X + M[1][1] * V.Y + M[1][2] * V.Z,
			M[2][0] * V.X + M[2][1] * V.Y + M[2][2] * V.Z);
	}

	FMatrix33 FMatrix33::GetTransposed() const
	{
		FMatrix33 Result;
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Column = 0; Column < 3; ++Column)
			{
				Result.M[Row][Column] = M[Column][Row];
			}
		}
		return Result;
	}

	double FMatrix33::Determinant() const
	{
		return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1])
			- M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
			+ M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
	}

	FQuat QuatFromRotationMatrix(const FMatrix33& R)
	{
		const double Trace = R.M[0][0] + R.M[1][1] + R.M[2][2];
		FQuat Q;
		if (Trace > 0.0)
		{
			const double S = FMath::Sqrt(Trace + 1.0) * 2.0;
			Q = FQuat((R.M[2][1] - R.M[1][2]) / S, (R.M[0][2] - R.M[2][0]) / S, (R.M[1][0] - R.M[0][1]) / S, 0.25 * S);
		}
		else if (R.M[0][0] > R.M[1][1] && R.M[0][0] > R.M[2][2])
		{
			const double S = FMath::Sqrt(1.0 + R.M[0][0] - R.M[1][1] - R.M[2][2]) * 2.0;
			Q = FQuat(0.25 * S, (R.M[0][1] + R.M[1][0]) / S, (R.M[0][2] + R.M[2][0]) / S, (R.M[2][1] - R.M[1][2]) / S);
		}
		else if (R.M[1][1] > R.M[2][2])
		{
			const double S = FMath::Sqrt(1.0 + R.M[1][1] - R.M[0][0] - R.M[2][2]) * 2.0;
			Q = FQuat((R.M[0][1] + R.M[1][0]) / S, 0.25 * S, (R.M[1][2] + R.M[2][1]) / S, (R.M[0][2] - R.M[2][0]) / S);
		}
		else
		{
			const double S = FMath::Sqrt(1.0 + R.M[2][2] - R.M[0][0] - R.M[1][1]) * 2.0;
			Q = FQuat((R.M[0][2] + R.M[2][0]) / S, (R.M[1][2] + R.M[2][1]) / S, 0.25 * S, (R.M[1][0] - R.M[0][1]) / S);
		}
		Q.Normalize();
		return Q;
	}

	FMatrix33 ComputeWorldSpaceInertia(const FQuat& Rotation, const FVector& Diagonal)
	{
		const FMatrix33 R = FMatrix33::FromQuat(Rotation);
		return R * FMatrix33::Diagonal(Diagonal) * R.GetTransposed();
	}

	FMatrix33 DiagonalizeSymmetric(const FMatrix33& A, FVector& OutEigenvalues)
	{
		// Cyclic Jacobi (e.g. Golub & Van Loan 8.5): each rotation zeroes one off-diagonal pair; quadratic convergence.
		FMatrix33 D = A;
		FMatrix33 V = FMatrix33::Identity();
		for (int32 Sweep = 0; Sweep < 64; ++Sweep)
		{
			const double Off = FMath::Square(D.M[0][1]) + FMath::Square(D.M[0][2]) + FMath::Square(D.M[1][2]);
			const double Scale = FMath::Square(D.M[0][0]) + FMath::Square(D.M[1][1]) + FMath::Square(D.M[2][2]);
			if (Off == 0.0 || Off <= 1e-36 * Scale)
			{
				break;
			}
			for (int32 P = 0; P < 2; ++P)
			{
				for (int32 Q = P + 1; Q < 3; ++Q)
				{
					const double Apq = D.M[P][Q];
					if (Apq == 0.0)
					{
						continue;
					}
					const double Theta = (D.M[Q][Q] - D.M[P][P]) / (2.0 * Apq);
					const double T = (Theta >= 0.0 ? 1.0 : -1.0) / (FMath::Abs(Theta) + FMath::Sqrt(Theta * Theta + 1.0));
					const double C = 1.0 / FMath::Sqrt(T * T + 1.0);
					const double S = T * C;
					// D <- J^T D J, V <- V J, with J the rotation in the (P, Q) plane.
					for (int32 K = 0; K < 3; ++K)
					{
						const double Dkp = D.M[K][P];
						const double Dkq = D.M[K][Q];
						D.M[K][P] = C * Dkp - S * Dkq;
						D.M[K][Q] = S * Dkp + C * Dkq;
					}
					for (int32 K = 0; K < 3; ++K)
					{
						const double Dpk = D.M[P][K];
						const double Dqk = D.M[Q][K];
						D.M[P][K] = C * Dpk - S * Dqk;
						D.M[Q][K] = S * Dpk + C * Dqk;
					}
					D.M[P][Q] = 0.0;
					D.M[Q][P] = 0.0;
					for (int32 K = 0; K < 3; ++K)
					{
						const double Vkp = V.M[K][P];
						const double Vkq = V.M[K][Q];
						V.M[K][P] = C * Vkp - S * Vkq;
						V.M[K][Q] = S * Vkp + C * Vkq;
					}
				}
			}
		}
		if (V.Determinant() < 0.0)
		{
			for (int32 K = 0; K < 3; ++K)
			{
				V.M[K][2] = -V.M[K][2];
			}
		}
		OutEigenvalues = D.GetDiagonal();
		return V;
	}
}
