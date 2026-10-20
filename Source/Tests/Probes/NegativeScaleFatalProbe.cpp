// NegativeScaleFatalProbe.cpp - CTest PhysicsTest.Probe.NegativeScaleFatal.
// Composing FTransforms with a negative scale component must stop the process in EVERY build: UE composes those
// through its matrix path (TTransform::MultiplyUsingMatrixWithScale), which the harness does not implement, and with
// PT_DO_CHECK=0 (test preset) a checkf would be compiled out. Expected: a Fatal log line and exit code 3
// (CheckFatalExit.cmake). Reaching the end of main() is the failure case.
#include "CoreMinimal.h"

#include <cstdio>

int main()
{
	using namespace PhysicsTest;
	FPlatformMisc::SuppressErrorDialogs();

	const FTransform Mirrored(FQuat::Identity, FVector::ZeroVector, FVector(-1.0, 1.0, 1.0));
	const FTransform Composed = Mirrored * FTransform::Identity;
	std::printf("NegativeScaleFatal: composition returned (scale X %f): the guard did not fire\n", Composed.GetScale3D().X);
	return 0;
}
