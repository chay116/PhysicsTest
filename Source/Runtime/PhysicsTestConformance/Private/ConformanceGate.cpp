// ConformanceGate.cpp - FGateReport.
#include "ConformanceGate.h"

namespace PhysicsTest
{
	const char* ToString(const EGateResult Result)
	{
		switch (Result)
		{
		case EGateResult::Pass: return "Pass";
		case EGateResult::Fail: return "Fail";
		case EGateResult::Skip: return "Skip";
		}
		return "?";
	}

	bool FGateReport::Check(const bool bOk, const std::string& What)
	{
		++NumChecks;
		if (!bOk)
		{
			Result = EGateResult::Fail;
			Failures.push_back(What);
		}
		return bOk;
	}

	std::string FGateReport::Summary() const
	{
		std::string Text = Gate + " " + Target + ": " + ToString(Result) + ", " + std::to_string(NumChecks) + " checks";
		for (const std::string& Failure : Failures)
		{
			Text += "\n  FAIL " + Failure;
		}
		for (const std::string& Line : Notes)
		{
			Text += "\n  note " + Line;
		}
		return Text;
	}
}
