// ConformanceGate.h - gate results (02_Architecture 14, A.12). [MVP M2: the ABI gates A0-A7, A10; M6: G gates]
// A gate collects named checks; it passes when every check passed. IConformanceGate (A.12) arrives with the G gates and
// the bench -Conformance mode (M6); the M2 ABI gates are free functions over a backend's C table (Abi/AbiGates.h).
#pragma once

#include "CoreTypes.h"

#include <string>
#include <vector>

namespace PhysicsTest
{
	enum class EGateResult : uint8
	{
		Pass,
		Fail,
		Skip,
	};

	[[nodiscard]] const char* ToString(EGateResult Result);

	struct FGateReport
	{
		std::string Gate;                 // "A0".."A10" (no A8 before M14), "G0".."G12"
		std::string Target;               // e.g. "Null (static)", "Null (DLL)"
		EGateResult Result = EGateResult::Pass;
		int32 NumChecks = 0;
		std::vector<std::string> Failures;
		std::vector<std::string> Notes;   // informative values (not pass / fail)

		// Records one check; a failed one turns the report into Fail.
		bool Check(bool bOk, const std::string& What);
		void Note(const std::string& What) { Notes.push_back(What); }
		[[nodiscard]] bool Passed() const { return Result == EGateResult::Pass; }
		// "A4 Null (static): Pass, 41 checks" + one line per failure.
		[[nodiscard]] std::string Summary() const;
	};
}
