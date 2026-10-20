// PhysicsConsoleVariables.h - the physics console variables the harness itself implements (UE names and defaults).
// [MVP M1]
//
// They are defined in one translation unit (PhysicsConsoleVariables.cpp) that the profile resolver references, so
// every program that resolves a profile registers all of them, whichever other PhysicsCore objects the linker pulls
// from the static library. The resolver treats exactly this table as "harness CVars" (values validated and
// normalised, pinned against backend variants, part of physics_sha256); any other name in [ConsoleVariables] or
// -ExecCmds is an engine CVar, so the resolved config does not depend on what else a program registers (test or
// viewer variables). 02_Architecture 10 (M1 memo).
//
//   p.ComNudgeAffectsInertia             bool, true    BodyUtils step 8 (UE BodyUtils.cpp:16-17 [V])
//   p.SensitiveSleepThresholdMultiplier  float, 1/20   ESleepFamily::Sensitive (UE BodyInstance.cpp:106-110 [V])
#pragma once

#include "HAL/IConsoleManager.h"

#include <span>
#include <string_view>

namespace PhysicsTest::PhysicsCVars
{
	[[nodiscard]] bool GetComNudgeAffectsInertia();
	[[nodiscard]] float GetSensitiveSleepThresholdMultiplier();

	// Every harness physics console variable, in name order.
	[[nodiscard]] std::span<IConsoleVariable* const> GetHarnessConsoleVariables();
	// The harness physics console variable called Name (case-insensitive, as UE), or nullptr when Name is not one.
	[[nodiscard]] IConsoleVariable* FindHarnessConsoleVariable(std::string_view Name);
}
