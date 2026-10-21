// PhysicsFiltering.h - UE4's PhysX filter data and simulation filter shader, re-implemented clean room from the design
// text (02_Architecture 8.2 "필터 셰이더", A.14). [MVP M5]   PT_BACKEND_NS=PhysX
//
// Filter words of every PxShape [M]:
//   word0  body index
//   word1  block mask (one bit per collision channel the body blocks; 32 channels, PT_NUM_COLLISION_CHANNELS)
//   word2  component id (UE: the owning component; the harness has no components, so the body index again)
//   word3  [6 bits extra mask filter][5 bits collision channel][21 bits EPDF flags], top to bottom
//          (UE5 PhysicsFiltering.h:18-35, EngineTypes.h:1063 NumExtraFilterBits = 6 [V])
// Shader [M]: a kinematic-kinematic pair is suppressed unless a shape asks for kinematic pairs
// (EPDF_KinematicKinematicPairs; the scene keeps such pairs, PxPairFilteringMode::eKEEP, so the shader decides); a
// kinematic-static pair and a pair that does not block mutually (each body's block mask has the other's channel) follow
// FPhysXFilterShaderRules below; every other pair is eCONTACT_DEFAULT, plus eDETECT_CCD_CONTACT when a shape has EPDF_CCD
// and the touch / contact-point notifications when a shape has EPDF_ContactNotify (only when the listener is installed: viz runs or PT_INSTR_CONTACTS, 02 9.4).
//
// The design text (02 8.2) does not say how UE4 filters those two kinds of pair, so both are [M] rules kept switchable
// through variant keys (the user checks them in UE 4.27, 03 2) and passed to the shader as PxSceneDesc::filterShaderData
// (PhysX copies the block; PxScene::getFilterShaderData reads it back for the effective key physx.filter_shader_rules):
//   NonBlockingPairFilter      Kill (default: dropped until the bounds separate) | Suppress (kept filtered, filtered again
//                              when a shape's filter data change)
//   KinematicStaticPairFilter  UnlessKinematicPairs (default: suppressed unless a shape has EPDF_KinematicKinematicPairs)
//                              | Always (suppressed whatever the flags say)
// No M5 scene exercises either (every body blocks every channel, no shape asks for kinematic pairs). The defaults are the
// reading of the design text; the alternatives are a reviewer's recollection of UE4 (Progress M5 round 0), unconfirmed.
#pragma once

#include "PhysXIncludes.h"
#include "PtTypes.h"

namespace PhysicsTest::PhysXBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	// UE5 PhysicsFiltering.h:18-28 [V] (the PhysX-era flag values, kept by UE5 for the legacy word3).
	enum EPhysXFilterDataFlags : uint32
	{
		EPDF_SimpleCollision = 0x0001,
		EPDF_ComplexCollision = 0x0002,
		EPDF_CCD = 0x0004,
		EPDF_ContactNotify = 0x0008,
		EPDF_StaticShape = 0x0010,
		EPDF_ModifyContacts = 0x0020,
		EPDF_KinematicKinematicPairs = 0x0040,
	};

	inline constexpr uint32 NumExtraFilterBits = 6;        // EngineTypes.h:1063 [V]
	inline constexpr uint32 NumCollisionChannelBits = 5;   // PhysicsFiltering.h:34 [V]
	inline constexpr uint32 NumFilterDataFlagBits = 32 - NumExtraFilterBits - NumCollisionChannelBits;   // 21, :35 [V]

	inline uint32 MakeWord3(const uint32 Channel, const uint32 ExtraMaskFilter, const uint32 Flags)
	{
		const uint32 ChannelAndFilter = ((ExtraMaskFilter & ((1u << NumExtraFilterBits) - 1u)) << NumCollisionChannelBits) | (Channel & ((1u << NumCollisionChannelBits) - 1u));
		return (ChannelAndFilter << NumFilterDataFlagBits) | (Flags & ((1u << NumFilterDataFlagBits) - 1u));
	}
	inline uint32 GetCollisionChannel(const uint32 Word3)
	{
		return (Word3 >> NumFilterDataFlagBits) & ((1u << NumCollisionChannelBits) - 1u);
	}
	inline uint32 GetExtraMaskFilter(const uint32 Word3)
	{
		return Word3 >> (32u - NumExtraFilterBits);
	}
	inline uint32 GetFilterDataFlags(const uint32 Word3)
	{
		return Word3 & ((1u << NumFilterDataFlagBits) - 1u);
	}

	// The two [M] outcomes the variant chooses (see the file comment). Plain uint32 fields: PhysX copies the block
	// (PxSceneDesc::filterShaderData) and the effective settings read it back.
	enum class EPhysXNonBlockingPairFilter : uint32
	{
		Kill = 0,       // PxFilterFlag::eKILL
		Suppress = 1,   // PxFilterFlag::eSUPPRESS
	};

	enum class EPhysXKinematicStaticPairFilter : uint32
	{
		UnlessKinematicPairs = 0,   // eSUPPRESS unless a shape has EPDF_KinematicKinematicPairs
		Always = 1,                 // eSUPPRESS
	};

	struct FPhysXFilterShaderRules
	{
		uint32 NonBlocking = static_cast<uint32>(EPhysXNonBlockingPairFilter::Kill);
		uint32 KinematicStatic = static_cast<uint32>(EPhysXKinematicStaticPairFilter::UnlessKinematicPairs);
	};

	// The rules of a constant block: the defaults when the block is absent or not an FPhysXFilterShaderRules.
	FPhysXFilterShaderRules ReadFilterShaderRules(const void* ConstantBlock, physx::PxU32 ConstantBlockSize);
	const char* ToString(EPhysXNonBlockingPairFilter Value);
	const char* ToString(EPhysXKinematicStaticPairFilter Value);

	// The simulation filter data of one shape (the query filter data uses the same words in this harness).
	physx::PxFilterData MakeSimFilterData(uint32 BodyIndex, uint32 BlockMask, uint32 ComponentId, uint32 Channel, uint32 EpdfFlags);

	physx::PxFilterFlags PhysXSimFilterShader(physx::PxFilterObjectAttributes Attributes0, physx::PxFilterData FilterData0,
		physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData FilterData1, physx::PxPairFlags& PairFlags, const void* ConstantBlock,
		physx::PxU32 ConstantBlockSize);
}
