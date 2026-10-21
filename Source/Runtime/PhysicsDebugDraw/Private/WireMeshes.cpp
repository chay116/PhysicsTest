// WireMeshes.cpp - unit wire meshes with UE's line batcher topology (see WireMeshes.h). The harness's own code.
#include "WireMeshes.h"

#include "Math/UnrealMathUtility.h"

#include <cmath>

namespace PhysicsTest::DebugDraw
{
	namespace
	{
		void AddSegment(std::vector<FWireVertex>& Out, const FWireVertex& A, const FWireVertex& B)
		{
			Out.push_back(A);
			Out.push_back(B);
		}
	}

	void AppendUnitBox(std::vector<FWireVertex>& Out)
	{
		// The top face (+Z), the bottom face (-Z), then the four verticals - the order of ULineBatchComponent::DrawBox.
		const float Corners[4][2] = { { 1.0f, 1.0f }, { 1.0f, -1.0f }, { -1.0f, -1.0f }, { -1.0f, 1.0f } };
		for (const float Z : { 1.0f, -1.0f })
		{
			for (int32 Edge = 0; Edge < 4; ++Edge)
			{
				const float* A = Corners[Edge];
				const float* B = Corners[(Edge + 1) % 4];
				AddSegment(Out, FWireVertex{ A[0], A[1], Z, 0.0f }, FWireVertex{ B[0], B[1], Z, 0.0f });
			}
		}
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			AddSegment(Out, FWireVertex{ Corners[Corner][0], Corners[Corner][1], 1.0f, 0.0f }, FWireVertex{ Corners[Corner][0], Corners[Corner][1], -1.0f, 0.0f });
		}
	}

	void AppendUnitSphere(int32 Segments, std::vector<FWireVertex>& Out)
	{
		// Written from the shape of the line batcher's sphere (LineBatchComponent.cpp:426-466 [V]), not from its code: a grid
		// of N polar bands x N azimuth steps on the unit sphere, angles in multiples of 2 pi / N. The polar angle starts at the
		// +Z pole and runs over the whole circle (0 .. 2 pi), so the far half of each band passes the -Z pole and comes back up
		// on the opposite meridian. Each grid cell emits two segments from its corner (polar b, azimuth k): the parallel to
		// azimuth k + 1, then the meridian to polar b + 1 - 2 * N * N segments in all.
		const int32 Count = Segments < 4 ? 4 : Segments;
		const double Step = 2.0 * UE_DOUBLE_PI / static_cast<double>(Count);
		const auto OnSphere = [Step](const int32 PolarIndex, const int32 AzimuthIndex)
		{
			const double Polar = Step * static_cast<double>(PolarIndex);
			const double Azimuth = Step * static_cast<double>(AzimuthIndex);
			const double Ring = std::sin(Polar);   // signed distance from the Z axis
			return FWireVertex{ static_cast<float>(Ring * std::cos(Azimuth)), static_cast<float>(Ring * std::sin(Azimuth)), static_cast<float>(std::cos(Polar)), 0.0f };
		};
		for (int32 Band = 0; Band < Count; ++Band)
		{
			for (int32 Azimuth = 0; Azimuth < Count; ++Azimuth)
			{
				const FWireVertex Corner = OnSphere(Band, Azimuth);
				AddSegment(Out, Corner, OnSphere(Band, Azimuth + 1));   // along the parallel
				AddSegment(Out, Corner, OnSphere(Band + 1, Azimuth));   // along the meridian
			}
		}
	}

	void AppendUnitCapsule(int32 Sides, std::vector<FWireVertex>& Out)
	{
		Sides = Sides < 2 ? 2 : Sides;
		const double SideAngle = 2.0 * UE_DOUBLE_PI / static_cast<double>(Sides);   // the angle one side spans
		const auto Circle = [&](const float Cap)
		{
			for (int32 Side = 0; Side < Sides; ++Side)
			{
				const double A0 = SideAngle * Side;
				const double A1 = SideAngle * (Side + 1);
				AddSegment(Out, FWireVertex{ static_cast<float>(std::cos(A0)), static_cast<float>(std::sin(A0)), 0.0f, Cap },
					FWireVertex{ static_cast<float>(std::cos(A1)), static_cast<float>(std::sin(A1)), 0.0f, Cap });
			}
		};
		// UE AddHalfCircle(Base, X, Y): Base + (X cos a + Y sin a) * R for a in [0, pi] (Sides / 2 segments).
		const auto HalfCircle = [&](const int32 InPlaneAxis, const float Cap)
		{
			for (int32 Side = 0; Side < Sides / 2; ++Side)
			{
				const double A0 = SideAngle * Side;
				const double A1 = SideAngle * (Side + 1);
				const auto Point = [&](const double Angle)
				{
					const float C = static_cast<float>(std::cos(Angle));
					const float S = static_cast<float>(std::sin(Angle)) * Cap;
					return InPlaneAxis == 0 ? FWireVertex{ C, 0.0f, S, Cap } : FWireVertex{ 0.0f, C, S, Cap };
				};
				AddSegment(Out, Point(A0), Point(A1));
			}
		};
		// DrawCapsule order: top and bottom circles, the domed caps (Y-Z and X-Z planes, top then bottom), 4 connecting lines.
		Circle(1.0f);
		Circle(-1.0f);
		HalfCircle(1, 1.0f);
		HalfCircle(0, 1.0f);
		HalfCircle(1, -1.0f);
		HalfCircle(0, -1.0f);
		const float Directions[4][2] = { { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f } };
		for (const auto& Direction : Directions)
		{
			AddSegment(Out, FWireVertex{ Direction[0], Direction[1], 0.0f, 1.0f }, FWireVertex{ Direction[0], Direction[1], 0.0f, -1.0f });
		}
	}

	void AppendUnitCircle(int32 Segments, std::vector<FWireVertex>& Out)
	{
		// DrawDebugCircle(matrix): Centre + R * (AxisY cos a + AxisZ sin a), starting at +Y.
		Segments = Segments < 4 ? 4 : Segments;
		const double AngleStep = 2.0 * UE_DOUBLE_PI / static_cast<double>(Segments);
		for (int32 Step = 0; Step < Segments; ++Step)
		{
			const double A0 = AngleStep * Step;
			const double A1 = AngleStep * (Step + 1);
			AddSegment(Out, FWireVertex{ 0.0f, static_cast<float>(std::cos(A0)), static_cast<float>(std::sin(A0)), 0.0f },
				FWireVertex{ 0.0f, static_cast<float>(std::cos(A1)), static_cast<float>(std::sin(A1)), 0.0f });
		}
	}
}
