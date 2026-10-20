// PtJoltSeam.h - engine-agnostic interface of the flag-leak probe library (03_WorkPlan M0 criterion 5).
// No Jolt include here: consumers must not need JPH_* defines or /arch:AVX2.
#pragma once

struct FPtJoltSeamResult
{
	double FinalZ = 0.0;                     // m, after the requested steps of free fall from z = 10 m
	const char* JoltConfiguration = "";      // JPH::GetConfigurationString() as seen INSIDE the library TU
	const char* BuildReportJson = "";        // PtBuildReport.h self-report of the library TU
};

FPtJoltSeamResult PtJoltSeamDropBox(int NumSteps);
