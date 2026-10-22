/* PtAbiC11Check.c - compiles PtBackendAbi.h as C11 with /W4 /WX (03_WorkPlan M2 criterion 1). The header is the only
   one shared with the Chaos DLL and must stay plain C: every structure size assert is evaluated here by a C compiler. */
#include "PhysicsTestAbi/PtBackendAbi.h"

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
	#error "PtAbiC11Check must be compiled as C11 (/std:c11)"
#endif

PT_STATIC_ASSERT(sizeof(PtVec3) == 24, "abi");
PT_STATIC_ASSERT(sizeof(PtQuat) == 32, "abi");
PT_STATIC_ASSERT(sizeof(PtTransform) == 56, "abi");
PT_STATIC_ASSERT(sizeof(PtFrameResult) == 16, "abi");
PT_STATIC_ASSERT(PT_ABI_VERSION_MAJOR == 1u, "M6: the frozen core v1.0 (abi-v1.0)");
PT_STATIC_ASSERT(PT_ABI_VERSION_MINOR == 1u, "M9: v1.1 (abi-v1.1: the formerly provisional entries final, layouts unchanged)");
PT_STATIC_ASSERT(PT_ABI_PROVISIONAL_REVISION == 3u, "M9: the revision is frozen at 3 since v1.1");
PT_STATIC_ASSERT(sizeof(PtConstraintCreateInfo) == 144 && sizeof(PtJointSnapshot) == 168 && sizeof(PtDebugDrawCmd) == 160, "v1.1: the v1.0 layouts");
PT_STATIC_ASSERT(PT_JOINTSNAP_ALL == 0x7u, "v1.1: PT_JOINTSNAP_ANGULAR_IMPULSE_NA appended");
PT_STATIC_ASSERT(PT_SNAP_ALL == 0x7Fu && PT_SNAP_ALL == (PT_SNAP_PARTICLES | PT_SNAP_BOUNDS | PT_SNAP_CONTACTS | PT_SNAP_JOINTS | PT_SNAP_NATIVE_ISLANDS
	| PT_SNAP_SLEEP_INFO | PT_SNAP_NATIVE_DRAW), "v1.1: PT_SNAP_ALL = every defined request bit (M9 fix round 1)");
PT_STATIC_ASSERT(PT_ABI_V1_0_API_SIZE == sizeof(PtBackendApi), "v1.1 appends no entry: the v1.0 table is still the whole table");

/* PT_API_HAS must compile as C (a v1.x host written in C reads appended entries through it). */
int PtAbiC11Check_HasLastEntry(const PtBackendApi* Api);
int PtAbiC11Check_HasLastEntry(const PtBackendApi* Api)
{
	return PT_API_HAS(Api, GetLastErrorMessage) ? 1 : 0;
}

/* A C translation unit must contain at least one external declaration that is not only a typedef or an assert. */
const PtBackendApi* PtAbiC11Check_TableType(void);
const PtBackendApi* PtAbiC11Check_TableType(void)
{
	return NULL;
}
