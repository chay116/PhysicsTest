// PtThirdPartyIncludesStart.h - HARNESS side; no include guard, always paired with PtThirdPartyIncludesEnd.h.
// (UE analogue: THIRD_PARTY_INCLUDES_START, which only pushes warnings. The harness also has to park its
// check / verify / ensure macros, and a macro cannot emit #undef, hence an include-file pair - DL03.)
//
//   #include "CoreMinimal.h"
//   #include "Misc/PtThirdPartyIncludesStart.h"
//   #include <doctest/doctest.h>        // also <imgui.h>, <implot.h>, <nlohmann/json.hpp>
//   #include "Misc/PtThirdPartyIncludesEnd.h"
//
// doctest v2.5.3 declares a member function check() (doctest.h:1647). The CTest probe
// PhysicsTest.Probe.ThirdPartyIncludes compiles exactly this sequence.
// Backend TUs never include harness Core; they use BackendCommon's PtThirdPartyIncludes.h (warnings only).
#pragma warning(push, 0)
#pragma push_macro("check")
#pragma push_macro("checkf")
#pragma push_macro("checkNoEntry")
#pragma push_macro("verify")
#pragma push_macro("verifyf")
#pragma push_macro("ensure")
#pragma push_macro("ensureMsgf")
#undef check
#undef checkf
#undef checkNoEntry
#undef verify
#undef verifyf
#undef ensure
#undef ensureMsgf
