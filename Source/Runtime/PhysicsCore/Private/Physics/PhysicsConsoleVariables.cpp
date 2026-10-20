// PhysicsConsoleVariables.cpp - definitions of the harness physics console variables (see Physics/PhysicsConsoleVariables.h).
// Keep every harness physics CVar in this file: a CVar registered by a static object in another PhysicsCore TU exists
// only in programs whose linker happens to pull that object file from the static library.
#include "Physics/PhysicsConsoleVariables.h"

namespace PhysicsTest::PhysicsCVars
{
	namespace
	{
		// UE BodyUtils.cpp:16-17 [V] (bPhysicsComNudgeAdjustInertia).
		bool bComNudgeAffectsInertia = true;
		FAutoConsoleVariableRef CVarComNudgeAffectsInertia(
			"p.ComNudgeAffectsInertia", bComNudgeAffectsInertia,
			"BodyUtils step 8: add the COMNudge parallel-axis term to the inertia (UE BodyUtils.cpp:16-17).");

		// UE BodyInstance.cpp:106-110 [V].
		float SensitiveSleepThresholdMultiplier = 1.0f / 20.0f;
		FAutoConsoleVariableRef CVarSensitiveSleepThresholdMultiplier(
			"p.SensitiveSleepThresholdMultiplier", SensitiveSleepThresholdMultiplier,
			"The sleep threshold multiplier of bodies in the Sensitive sleep family (UE BodyInstance.cpp:106-110).");

		// Dynamic initialisation runs in definition order inside one TU, so both variables exist here. Name order.
		IConsoleVariable* const GHarnessConsoleVariables[] = {
			CVarComNudgeAffectsInertia.AsVariable(),
			CVarSensitiveSleepThresholdMultiplier.AsVariable(),
		};

		bool EqualsNoCase(const std::string_view A, const std::string_view B)
		{
			if (A.size() != B.size())
			{
				return false;
			}
			for (size_t Index = 0; Index < A.size(); ++Index)
			{
				const char CA = (A[Index] >= 'A' && A[Index] <= 'Z') ? static_cast<char>(A[Index] - 'A' + 'a') : A[Index];
				const char CB = (B[Index] >= 'A' && B[Index] <= 'Z') ? static_cast<char>(B[Index] - 'A' + 'a') : B[Index];
				if (CA != CB)
				{
					return false;
				}
			}
			return true;
		}
	}

	bool GetComNudgeAffectsInertia()
	{
		return bComNudgeAffectsInertia;
	}

	float GetSensitiveSleepThresholdMultiplier()
	{
		return SensitiveSleepThresholdMultiplier;
	}

	std::span<IConsoleVariable* const> GetHarnessConsoleVariables()
	{
		return GHarnessConsoleVariables;
	}

	IConsoleVariable* FindHarnessConsoleVariable(const std::string_view Name)
	{
		for (IConsoleVariable* Variable : GHarnessConsoleVariables)
		{
			if (EqualsNoCase(Variable->GetName(), Name))
			{
				return Variable;
			}
		}
		return nullptr;
	}
}
