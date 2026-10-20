#pragma once
// Engine-agnostic header: no Jolt includes, so consumers need no JPH_* defines or /arch flags.
struct PTDropResult
{
	double FinalY;      // m
	double MedianStepUs;
	const char *Config; // Jolt GetConfigurationString() as seen INSIDE the backend TU
};

PTDropResult PT_JoltDropBox(int inNumSteps, int inNumWorkers);
