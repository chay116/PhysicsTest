// PhysicsFiltering.cpp - see PhysicsFiltering.h.
#include "PhysicsFiltering.h"

#include <string.h>

namespace PhysicsTest::PhysXBackend
{
	FPhysXFilterShaderRules ReadFilterShaderRules(const void* const ConstantBlock, const physx::PxU32 ConstantBlockSize)
	{
		FPhysXFilterShaderRules Rules;
		if (ConstantBlock != nullptr && ConstantBlockSize == sizeof(FPhysXFilterShaderRules))
		{
			memcpy(&Rules, ConstantBlock, sizeof(Rules));
		}
		return Rules;
	}

	const char* ToString(const EPhysXNonBlockingPairFilter Value)
	{
		return Value == EPhysXNonBlockingPairFilter::Suppress ? "Suppress" : "Kill";
	}

	const char* ToString(const EPhysXKinematicStaticPairFilter Value)
	{
		return Value == EPhysXKinematicStaticPairFilter::Always ? "Always" : "UnlessKinematicPairs";
	}

	physx::PxFilterData MakeSimFilterData(const uint32 BodyIndex, const uint32 BlockMask, const uint32 ComponentId, const uint32 Channel, const uint32 EpdfFlags)
	{
		return physx::PxFilterData(BodyIndex, BlockMask, ComponentId, MakeWord3(Channel, 0u, EpdfFlags));
	}

	physx::PxFilterFlags PhysXSimFilterShader(const physx::PxFilterObjectAttributes Attributes0, const physx::PxFilterData FilterData0,
		const physx::PxFilterObjectAttributes Attributes1, const physx::PxFilterData FilterData1, physx::PxPairFlags& PairFlags, const void* ConstantBlock,
		const physx::PxU32 ConstantBlockSize)
	{
		const FPhysXFilterShaderRules Rules = ReadFilterShaderRules(ConstantBlock, ConstantBlockSize);
		const uint32 Flags0 = GetFilterDataFlags(FilterData0.word3);
		const uint32 Flags1 = GetFilterDataFlags(FilterData1.word3);
		const uint32 Either = Flags0 | Flags1;

		// Kinematic-kinematic and kinematic-static pairs reach the shader because the scene keeps them (eKEEP); a pair with
		// no dynamic body has nothing to solve unless a shape asks for it. A kinematic-static pair follows the variant's [M]
		// rule (KinematicStaticPairFilter): as a kinematic-kinematic pair, or suppressed whatever the flags say.
		const bool bKinematic0 = physx::PxFilterObjectIsKinematic(Attributes0);
		const bool bKinematic1 = physx::PxFilterObjectIsKinematic(Attributes1);
		const bool bStatic0 = physx::PxGetFilterObjectType(Attributes0) == physx::PxFilterObjectType::eRIGID_STATIC;
		const bool bStatic1 = physx::PxGetFilterObjectType(Attributes1) == physx::PxFilterObjectType::eRIGID_STATIC;
		if ((bKinematic0 || bStatic0) && (bKinematic1 || bStatic1))
		{
			const bool bAlwaysSuppressed = (bStatic0 || bStatic1) && Rules.KinematicStatic == static_cast<uint32>(EPhysXKinematicStaticPairFilter::Always);
			if (bAlwaysSuppressed || (Either & EPDF_KinematicKinematicPairs) == 0)
			{
				return physx::PxFilterFlag::eSUPPRESS;
			}
		}

		// Mutual blocking: each body's block mask (word1) has the other body's channel. A pair that does not block follows the
		// variant's [M] rule (NonBlockingPairFilter): killed or suppressed.
		const uint32 Channel0 = GetCollisionChannel(FilterData0.word3);
		const uint32 Channel1 = GetCollisionChannel(FilterData1.word3);
		const bool bBlock = (FilterData0.word1 & (1u << Channel1)) != 0 && (FilterData1.word1 & (1u << Channel0)) != 0;
		if (!bBlock)
		{
			if (Rules.NonBlocking == static_cast<uint32>(EPhysXNonBlockingPairFilter::Suppress))
			{
				return physx::PxFilterFlag::eSUPPRESS;
			}
			return physx::PxFilterFlag::eKILL;
		}

		PairFlags = physx::PxPairFlag::eCONTACT_DEFAULT;
		if ((Either & EPDF_CCD) != 0)
		{
			PairFlags |= physx::PxPairFlag::eDETECT_CCD_CONTACT;
		}
		if ((Either & EPDF_ContactNotify) != 0)
		{
			PairFlags |= physx::PxPairFlag::eNOTIFY_TOUCH_FOUND | physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS | physx::PxPairFlag::eNOTIFY_CONTACT_POINTS;
		}
		if ((Either & EPDF_ModifyContacts) != 0)
		{
			PairFlags |= physx::PxPairFlag::eMODIFY_CONTACTS;
		}
		return physx::PxFilterFlag::eDEFAULT;
	}
}
