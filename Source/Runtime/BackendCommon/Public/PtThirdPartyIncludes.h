// PtThirdPartyIncludes.h - BACKEND side only (SDK headers in backend Private TUs, SDK smoke tests).
// Backend TUs never include harness Core, so check / verify / ensure are not defined there: warnings only (DL03).
// The harness-side pair that also parks those macros is Core's Misc/PtThirdPartyIncludesStart.h / End.h.
//
//   PT_THIRD_PARTY_INCLUDES_START
//   #include <Jolt/Jolt.h>
//   PT_THIRD_PARTY_INCLUDES_END
#pragma once

#define PT_THIRD_PARTY_INCLUDES_START __pragma(warning(push, 0))
#define PT_THIRD_PARTY_INCLUDES_END   __pragma(warning(pop))
