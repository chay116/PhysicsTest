// FreeFallFixture.h - the free-fall fixture of 03_WorkPlan M2: a sphere of radius 50 cm, 1 kg (bOverrideMass), at
// z0 = 1000 cm, damping 0, falling under the scene gravity (-980 cm/s^2) for 60 frames of FixedStep 1/60.
// Expected: z60 = 501.8332813705 cm within 1e-9 against dt_engine = (double)(float)(1/60) (DL23). The same fixture is the
// M3 scene S00.FreeFall and the M2.5 spike's SP3 check, so it is built through the FPhysScene API only.
#pragma once

#include "CoreTypes.h"

namespace PhysicsTest
{
	class FPhysScene;
	struct FBodyInstance;

	namespace Fixtures
	{
		struct FFreeFall
		{
			static constexpr double Radius = 50.0;   // cm
			static constexpr float MassKg = 1.0f;
			static constexpr double Z0 = 1000.0;     // cm
			static constexpr int32 Frames = 60;
			static constexpr double ExpectedZ60 = 501.8332813705;   // cm, dt_engine reference (DL23)
			static constexpr double Tolerance = 1e-9;               // cm

			// Spawns the body into Scene (and flushes the deferred actors). The scene must use FixedStep 1/60, gravity -980.
			static FBodyInstance* Build(FPhysScene& Scene);
			// The analytic semi-implicit Euler reference after Steps steps of the float StepDeltaTime (dt_engine).
			[[nodiscard]] static double ExpectedZ(int32 Steps, float StepDeltaTime, double GravityZ = -980.0);
		};
	}
}
