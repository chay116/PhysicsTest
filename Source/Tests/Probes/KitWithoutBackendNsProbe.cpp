// KitWithoutBackendNsProbe.cpp - must NOT compile: a backend TU that includes the Kit without PT_BACKEND_NS stops with
// #error (02_Architecture 4.5). Built only by the CTest PhysicsTest.Probe.KitNeedsBackendNs (EXCLUDE_FROM_ALL target).
#include "PhysicsTestAbi/PtBackendKit.h"

int PtProbeKitWithoutBackendNs()
{
	return 0;
}
