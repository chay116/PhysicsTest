// ThirdPartyIncludesProbe.cpp - CTest compile probe (03_WorkPlan M0 criterion 9, DL03, 02_Architecture 4.5).
// Harness Core is included FIRST (it defines the function-like macros check / verify / ensure), then doctest, ImGui,
// ImPlot and nlohmann/json through the harness-side include pair. doctest v2.5.3 declares a member function check()
// (doctest.h:1647): without the pair that declaration is macro-expanded and the TU does not compile.
#include "CoreMinimal.h"

#if !defined(check) || !defined(verify) || !defined(ensure)
	#error "Core must define check / verify / ensure before the third-party headers are included"
#endif

#include "Misc/PtThirdPartyIncludesStart.h"
#define DOCTEST_CONFIG_IMPLEMENT   // compile doctest's implementation too: it calls the member check() (doctest.h:4513)
#include <doctest/doctest.h>
#include <imgui.h>
#include <implot.h>
#include <nlohmann/json.hpp>
#include "Misc/PtThirdPartyIncludesEnd.h"

#if !defined(check) || !defined(checkf) || !defined(verify) || !defined(ensure) || !defined(ensureMsgf)
	#error "Misc/PtThirdPartyIncludesEnd.h must restore check / checkf / verify / ensure / ensureMsgf"
#endif

int main()
{
	using namespace PhysicsTest;
	FPlatformMisc::SuppressErrorDialogs();

	// Use each library a little so the probe is more than an include list.
	const nlohmann::json Json = nlohmann::json::parse(R"({"probe": 1})");
	const ImVec2 Size(2.0f, 3.0f);
	const ImPlotPoint Point(1.0, 2.0);
	static_assert(DOCTEST_VERSION_MAJOR == 2 && DOCTEST_VERSION_MINOR == 5, "doctest v2.5.x expected");

	// The restored Core macros work after the pair.
	const bool bJsonOk = ensure(Json.at("probe").get<int>() == 1);
	const bool bImGuiOk = ensure(Size.x + Size.y == 5.0f);
	const bool bImPlotOk = ensure(Point.y == 2.0);
	check(bJsonOk && bImGuiOk && bImPlotOk);

	UE_LOG(LogPhysicsTest, Display, "ThirdPartyIncludes probe: doctest %s, Dear ImGui %s, nlohmann/json %d.%d.%d - Core macros survived",
		DOCTEST_VERSION_STR, IMGUI_VERSION, NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR, NLOHMANN_JSON_VERSION_PATCH);
	return bJsonOk && bImGuiOk && bImPlotOk ? 0 : 1;
}
