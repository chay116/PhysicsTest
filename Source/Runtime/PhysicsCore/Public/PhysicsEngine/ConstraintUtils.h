// ConstraintUtils.h - joint presets with UE's names and signatures (UE: Engine/Classes/PhysicsEngine/ConstraintUtils.h:10,
// :19 [V]). [MVP M1: data only] Only the two Phase-1 presets exist (ball and hinge, DL31).
#pragma once

#include "PhysicsEngine/ConstraintInstance.h"

namespace PhysicsTest::ConstraintUtils
{
	// Linear locked, swing and twist free; bOverwriteLimits zeroes the three angular limits (ConstraintUtils.cpp:70-88 [V]).
	void ConfigureAsBallAndSocket(FConstraintInstance& ConstraintInstance, bool bOverwriteLimits = true);
	// Linear locked, swings locked, twist free; bOverwriteLimits sets swings 0 and twist 45 degrees (ConstraintUtils.cpp:8-27 [V]).
	void ConfigureAsHinge(FConstraintInstance& ConstraintInstance, bool bOverwriteLimits = true);
	// Linear locked and more than one non-locked angular DOF (ConstraintUtils.cpp:126-137 [V]).
	[[nodiscard]] bool IsBallAndSocket(const FConstraintInstance& ConstraintInstance);
	// Linear locked and exactly one non-locked angular DOF (ConstraintUtils.cpp:90-101 [V]).
	[[nodiscard]] bool IsHinge(const FConstraintInstance& ConstraintInstance);
}
