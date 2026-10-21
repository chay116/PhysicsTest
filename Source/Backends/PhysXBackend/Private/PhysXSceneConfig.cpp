// PhysXSceneConfig.cpp - the [PhysicsTest.Backend.PhysX] keys (see PhysXSceneConfig.h).
#include "PhysXSceneConfig.h"

#include "PhysXParse.h"

#include <stdio.h>

namespace PhysicsTest::PhysXBackend
{
	namespace Abi = ::PhysicsTest::Abi;

	namespace
	{
		template <class TEnum, size_t N>
		bool ParseEnum(const Abi::FStringRef Value, const char* const (&Names)[N], const TEnum (&Values)[N], TEnum& Out)
		{
			for (size_t Index = 0; Index < N; ++Index)
			{
				if (EqualsNoCase(Names[Index], Value))
				{
					Out = Values[Index];
					return true;
				}
			}
			return false;
		}

		PtResult Fail(char (&Error)[256], const Abi::FStringRef Key, const char* Expected)
		{
			snprintf(Error, sizeof(Error), "PhysX: scene option %.*s must be %s", static_cast<int>(Key.Length), Key.Data, Expected);
			return PT_E_INVALID_ARG;
		}

		PtResult ParsePositive(const Abi::FStringRef Key, const Abi::FStringRef Value, double& Out, char (&Error)[256], const bool bAllowZero = false)
		{
			double Parsed = 0.0;
			if (!ParseDouble(Value, Parsed) || Parsed < 0.0 || (!bAllowZero && Parsed == 0.0))
			{
				return Fail(Error, Key, bAllowZero ? "a finite number >= 0" : "a finite number > 0");
			}
			Out = Parsed;
			return PT_OK;
		}

		PtResult ParseFlag(const Abi::FStringRef Key, const Abi::FStringRef Value, bool& Out, char (&Error)[256])
		{
			return ParseBool(Value, Out) ? PT_OK : Fail(Error, Key, "true / false / 1 / 0");
		}

		PtResult ParseRange(const Abi::FStringRef Key, const Abi::FStringRef Value, const uint64 Min, const uint64 Max, uint32& Out, char (&Error)[256])
		{
			uint64 Parsed = 0;
			if (!ParseUInt(Value, Parsed) || Parsed < Min || Parsed > Max)
			{
				char Expected[64];
				snprintf(Expected, sizeof(Expected), "an integer in [%llu, %llu]", static_cast<unsigned long long>(Min), static_cast<unsigned long long>(Max));
				return Fail(Error, Key, Expected);
			}
			Out = static_cast<uint32>(Parsed);
			return PT_OK;
		}

		const char* const GFilteringNames[] = { "Keep", "Suppress", "Kill" };
		const physx::PxPairFilteringMode::Enum GFilteringValues[] = { physx::PxPairFilteringMode::eKEEP, physx::PxPairFilteringMode::eSUPPRESS, physx::PxPairFilteringMode::eKILL };
	}

	PtResult ApplySceneOption(FPhysXSceneConfig& Config, const Abi::FStringRef Key, const Abi::FStringRef Value, char (&Error)[256])
	{
		if (EqualsNoCase("LengthScale", Key))
		{
			double Parsed = 0.0;
			if (!ParseDouble(Value, Parsed) || Parsed < 1.0e-4 || Parsed > 1.0e2)
			{
				return Fail(Error, Key, "a number in [1e-4, 100] (native length units per cm: 1 = cm, 0.01 = m)");
			}
			Config.LengthScale = Parsed;
			return PT_OK;
		}
		if (EqualsNoCase("BroadPhaseType", Key))
		{
			const char* const Names[] = { "SAP", "ABP", "PABP", "eSAP", "eABP", "ePABP" };
			const physx::PxBroadPhaseType::Enum Values[] = { physx::PxBroadPhaseType::eSAP, physx::PxBroadPhaseType::eABP, physx::PxBroadPhaseType::ePABP,
				physx::PxBroadPhaseType::eSAP, physx::PxBroadPhaseType::eABP, physx::PxBroadPhaseType::ePABP };
			// eMBP needs user broad-phase regions (objects outside every region get no pairs) and eGPU a CUDA context: not offered.
			return ParseEnum(Value, Names, Values, Config.BroadPhase) ? PT_OK : Fail(Error, Key, "SAP, ABP or PABP (MBP needs regions, GPU a CUDA context)");
		}
		if (EqualsNoCase("SolverType", Key))
		{
			const char* const Names[] = { "PGS", "TGS", "ePGS", "eTGS" };
			const physx::PxSolverType::Enum Values[] = { physx::PxSolverType::ePGS, physx::PxSolverType::eTGS, physx::PxSolverType::ePGS, physx::PxSolverType::eTGS };
			return ParseEnum(Value, Names, Values, Config.Solver) ? PT_OK : Fail(Error, Key, "PGS or TGS");
		}
		if (EqualsNoCase("bEnablePCM", Key))
		{
			return ParseFlag(Key, Value, Config.bEnablePCM, Error);
		}
		if (EqualsNoCase("bEnableStabilization", Key))
		{
			return ParseFlag(Key, Value, Config.bEnableStabilization, Error);
		}
		if (EqualsNoCase("bEnableEnhancedDeterminism", Key))
		{
			return ParseFlag(Key, Value, Config.bEnableEnhancedDeterminism, Error);
		}
		if (EqualsNoCase("bEnableCCD", Key))
		{
			return ParseFlag(Key, Value, Config.bEnableCCD, Error);
		}
		if (EqualsNoCase("bEnableActiveActors", Key))
		{
			return ParseFlag(Key, Value, Config.bEnableActiveActors, Error);
		}
		if (EqualsNoCase("KineKineFilteringMode", Key))
		{
			return ParseEnum(Value, GFilteringNames, GFilteringValues, Config.KineKineFiltering) ? PT_OK : Fail(Error, Key, "Keep, Suppress or Kill");
		}
		if (EqualsNoCase("StaticKineFilteringMode", Key))
		{
			return ParseEnum(Value, GFilteringNames, GFilteringValues, Config.StaticKineFiltering) ? PT_OK : Fail(Error, Key, "Keep, Suppress or Kill");
		}
		if (EqualsNoCase("NonBlockingPairFilter", Key))
		{
			const char* const Names[] = { "Kill", "Suppress" };
			const EPhysXNonBlockingPairFilter Values[] = { EPhysXNonBlockingPairFilter::Kill, EPhysXNonBlockingPairFilter::Suppress };
			return ParseEnum(Value, Names, Values, Config.NonBlockingPairFilter) ? PT_OK : Fail(Error, Key, "Kill or Suppress");
		}
		if (EqualsNoCase("KinematicStaticPairFilter", Key))
		{
			const char* const Names[] = { "UnlessKinematicPairs", "Always" };
			const EPhysXKinematicStaticPairFilter Values[] = { EPhysXKinematicStaticPairFilter::UnlessKinematicPairs, EPhysXKinematicStaticPairFilter::Always };
			return ParseEnum(Value, Names, Values, Config.KinematicStaticPairFilter) ? PT_OK : Fail(Error, Key, "UnlessKinematicPairs or Always");
		}
		if (EqualsNoCase("BounceThresholdVelocity", Key))
		{
			return ParsePositive(Key, Value, Config.BounceThresholdVelocityCm, Error);
		}
		if (EqualsNoCase("DynamicTreeRebuildRateHint", Key))
		{
			return ParseRange(Key, Value, 4, 1u << 20, Config.DynamicTreeRebuildRateHint, Error);   // PxSceneQueryDesc::isValid: >= 4
		}
		if (EqualsNoCase("ScratchMemorySize", Key))
		{
			uint32 Bytes = 0;
			const PtResult Result = ParseRange(Key, Value, 0, 1u << 30, Bytes, Error);
			if (Result != PT_OK)
			{
				return Result;
			}
			if (Bytes % 16384u != 0)
			{
				return Fail(Error, Key, "a multiple of 16384 bytes (PxScene::simulate scratch block), 0 = none");
			}
			Config.ScratchMemoryBytes = Bytes;
			return PT_OK;
		}
		if (EqualsNoCase("PositionIterations", Key))
		{
			return ParseRange(Key, Value, 1, 255, Config.PositionIterations, Error);   // PxRigidDynamic::setSolverIterationCounts
		}
		if (EqualsNoCase("VelocityIterations", Key))
		{
			return ParseRange(Key, Value, 0, 255, Config.VelocityIterations, Error);
		}
		if (EqualsNoCase("ContactOffsetRule", Key))
		{
			const char* const Names[] = { "UE4Clamp", "SdkDefault" };
			const EPhysXContactOffsetRule Values[] = { EPhysXContactOffsetRule::UE4Clamp, EPhysXContactOffsetRule::SdkDefault };
			return ParseEnum(Value, Names, Values, Config.ContactOffsetRule) ? PT_OK : Fail(Error, Key, "UE4Clamp or SdkDefault");
		}
		if (EqualsNoCase("ContactOffsetMultiplier", Key))
		{
			return ParsePositive(Key, Value, Config.ContactOffsetMultiplier, Error, true);
		}
		if (EqualsNoCase("MinContactOffset", Key))
		{
			return ParsePositive(Key, Value, Config.MinContactOffsetCm, Error);
		}
		if (EqualsNoCase("MaxContactOffset", Key))
		{
			return ParsePositive(Key, Value, Config.MaxContactOffsetCm, Error);
		}
		if (EqualsNoCase("StaticFrictionRule", Key))
		{
			const char* const Names[] = { "EqualsDynamic", "MaxOfStaticAndDynamic" };
			const EPhysXStaticFrictionRule Values[] = { EPhysXStaticFrictionRule::EqualsDynamic, EPhysXStaticFrictionRule::MaxOfStaticAndDynamic };
			return ParseEnum(Value, Names, Values, Config.StaticFrictionRule) ? PT_OK : Fail(Error, Key, "EqualsDynamic or MaxOfStaticAndDynamic");
		}
		if (EqualsNoCase("SleepThresholdRule", Key))
		{
			const char* const Names[] = { "UE4Multiplier", "SdkDefault" };
			const EPhysXThresholdRule Values[] = { EPhysXThresholdRule::UE4Multiplier, EPhysXThresholdRule::SdkDefault };
			return ParseEnum(Value, Names, Values, Config.ThresholdRule) ? PT_OK : Fail(Error, Key, "UE4Multiplier or SdkDefault");
		}
		if (EqualsNoCase("MaxDepenetrationRule", Key))
		{
			const char* const Names[] = { "UE4Project", "SdkDefault" };
			const EPhysXDepenetrationRule Values[] = { EPhysXDepenetrationRule::UE4Project, EPhysXDepenetrationRule::SdkDefault };
			return ParseEnum(Value, Names, Values, Config.DepenetrationRule) ? PT_OK : Fail(Error, Key, "UE4Project or SdkDefault");
		}
		if (EqualsNoCase("ProjectMaxDepenetrationVelocity", Key))
		{
			return ParsePositive(Key, Value, Config.ProjectMaxDepenetrationVelocityCm, Error, true);
		}
		if (EqualsNoCase("CpuDispatcher", Key))
		{
			const char* const Names[] = { "Auto", "SingleThread", "PxDefault" };
			const EPhysXDispatcherKind Values[] = { EPhysXDispatcherKind::Auto, EPhysXDispatcherKind::SingleThread, EPhysXDispatcherKind::PxDefault };
			return ParseEnum(Value, Names, Values, Config.Dispatcher) ? PT_OK : Fail(Error, Key, "Auto, SingleThread or PxDefault");
		}
		if (EqualsNoCase("GyroscopicForces", Key))
		{
			const char* const Names[] = { "Ignore", "FromBody" };
			const EPhysXGyroscopicRule Values[] = { EPhysXGyroscopicRule::Ignore, EPhysXGyroscopicRule::FromBody };
			return ParseEnum(Value, Names, Values, Config.GyroscopicRule) ? PT_OK : Fail(Error, Key, "Ignore or FromBody");
		}
		snprintf(Error, sizeof(Error), "PhysX: unknown scene option '%.*s' (see Config/Profiles/Backend/PhysX.UE4Compat.ini for the keys)", static_cast<int>(Key.Length),
			Key.Data);
		return PT_E_INVALID_ARG;
	}

	bool IsRuntimeSceneOption(const Abi::FStringRef Key)
	{
		return EqualsNoCase("BounceThresholdVelocity", Key) || EqualsNoCase("DynamicTreeRebuildRateHint", Key);
	}

	const char* ToString(const physx::PxBroadPhaseType::Enum Value)
	{
		switch (Value)
		{
		case physx::PxBroadPhaseType::eSAP: return "eSAP";
		case physx::PxBroadPhaseType::eMBP: return "eMBP";
		case physx::PxBroadPhaseType::eABP: return "eABP";
		case physx::PxBroadPhaseType::ePABP: return "ePABP";
		case physx::PxBroadPhaseType::eGPU: return "eGPU";
		default: return "?";
		}
	}

	const char* ToString(const physx::PxSolverType::Enum Value)
	{
		return Value == physx::PxSolverType::eTGS ? "eTGS" : "ePGS";
	}

	const char* ToString(const physx::PxPairFilteringMode::Enum Value)
	{
		switch (Value)
		{
		case physx::PxPairFilteringMode::eKEEP: return "eKEEP";
		case physx::PxPairFilteringMode::eSUPPRESS: return "eSUPPRESS";
		case physx::PxPairFilteringMode::eKILL: return "eKILL";
		default: return "?";
		}
	}

	const char* ToString(const EPhysXContactOffsetRule Value)
	{
		return Value == EPhysXContactOffsetRule::UE4Clamp ? "UE4Clamp" : "SdkDefault";
	}

	const char* ToString(const EPhysXStaticFrictionRule Value)
	{
		return Value == EPhysXStaticFrictionRule::EqualsDynamic ? "EqualsDynamic" : "MaxOfStaticAndDynamic";
	}

	const char* ToString(const EPhysXThresholdRule Value)
	{
		return Value == EPhysXThresholdRule::UE4Multiplier ? "UE4Multiplier" : "SdkDefault";
	}

	const char* ToString(const EPhysXDepenetrationRule Value)
	{
		return Value == EPhysXDepenetrationRule::UE4Project ? "UE4Project" : "SdkDefault";
	}

	const char* ToString(const EPhysXDispatcherKind Value)
	{
		switch (Value)
		{
		case EPhysXDispatcherKind::Auto: return "Auto";
		case EPhysXDispatcherKind::SingleThread: return "SingleThread";
		case EPhysXDispatcherKind::PxDefault: return "PxDefault";
		}
		return "?";
	}

	const char* ToString(const EPhysXGyroscopicRule Value)
	{
		return Value == EPhysXGyroscopicRule::Ignore ? "Ignore" : "FromBody";
	}
}
