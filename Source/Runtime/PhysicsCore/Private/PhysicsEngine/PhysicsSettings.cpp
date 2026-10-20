// PhysicsSettings.cpp - enum names as they appear in ini files and logs (UE spelling for UE enums).
#include "PhysicsEngine/PhysicsSettings.h"

namespace PhysicsTest
{
	const char* ToString(const EPhysicsTickMode Value)
	{
		switch (Value)
		{
		case EPhysicsTickMode::FixedStep:   return "FixedStep";
		case EPhysicsTickMode::Sync:        return "Sync";
		case EPhysicsTickMode::Substepping: return "Substepping";
		case EPhysicsTickMode::AsyncFixed:  return "AsyncFixed";
		}
		return "?";
	}

	const char* ToString(const ESubstepClampRule Value)
	{
		switch (Value)
		{
		case ESubstepClampRule::UE5ChaosScene:  return "UE5ChaosScene";
		case ESubstepClampRule::UE4PhysSubstep: return "UE4PhysSubstep";
		}
		return "?";
	}

	const char* ToString(const EFrameDriver Value)
	{
		switch (Value)
		{
		case EFrameDriver::Auto:    return "Auto";
		case EFrameDriver::Harness: return "Harness";
		case EFrameDriver::Native:  return "Native";
		}
		return "?";
	}

	const char* ToString(const EPhysicsRunMode Value)
	{
		switch (Value)
		{
		case EPhysicsRunMode::Bench:    return "Bench";
		case EPhysicsRunMode::Behavior: return "Behavior";
		case EPhysicsRunMode::Viz:      return "Viz";
		}
		return "?";
	}

	const char* ToString(const EMassRule Value)
	{
		switch (Value)
		{
		case EMassRule::UEBodyUtils: return "UEBodyUtils";
		case EMassRule::Physical:    return "Physical";
		}
		return "?";
	}

	const char* ToString(const EChaosThreadingMode Value)
	{
		switch (Value)
		{
		case EChaosThreadingMode::DedicatedThread: return "DedicatedThread";
		case EChaosThreadingMode::TaskGraph:       return "TaskGraph";
		case EChaosThreadingMode::SingleThread:    return "SingleThread";
		}
		return "?";
	}

	const char* ToString(const EFrictionCombineMode Value)
	{
		switch (Value)
		{
		case EFrictionCombineMode::Average:  return "Average";
		case EFrictionCombineMode::Min:      return "Min";
		case EFrictionCombineMode::Multiply: return "Multiply";
		case EFrictionCombineMode::Max:      return "Max";
		}
		return "?";
	}

	const char* ToString(const ESleepFamily Value)
	{
		switch (Value)
		{
		case ESleepFamily::Normal:    return "Normal";
		case ESleepFamily::Sensitive: return "Sensitive";
		case ESleepFamily::Custom:    return "Custom";
		}
		return "?";
	}
}
