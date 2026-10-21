/* PtAbiC11Check.c - compiles PtBackendAbi.h as C11 with /W4 /WX (03_WorkPlan M2 criterion 1). The header is the only
   one shared with the Chaos DLL and must stay plain C: every structure size assert is evaluated here by a C compiler. */
#include "PhysicsTestAbi/PtBackendAbi.h"

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
	#error "PtAbiC11Check must be compiled as C11 (/std:c11)"
#endif

PT_STATIC_ASSERT(sizeof(PtVec3) == 24, "abi");
PT_STATIC_ASSERT(sizeof(PtQuat) == 32, "abi");
PT_STATIC_ASSERT(sizeof(PtTransform) == 56, "abi");
PT_STATIC_ASSERT(sizeof(PtFrameResult) == 8, "abi");
PT_STATIC_ASSERT(PT_ABI_VERSION_MAJOR == 0u, "M2: v0.9 until the M6 freeze");

/* A C translation unit must contain at least one external declaration that is not only a typedef or an assert. */
const PtBackendApi* PtAbiC11Check_TableType(void);
const PtBackendApi* PtAbiC11Check_TableType(void)
{
	return NULL;
}
