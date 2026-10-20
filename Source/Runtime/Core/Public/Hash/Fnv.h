// Fnv.h - FNV-1a hashes (UE: Core/Public/Hash/Fnv.h, FFnv::MemFnv32 / MemFnv64).
#pragma once

#include "CoreTypes.h"

namespace PhysicsTest
{
	struct FFnv
	{
		static constexpr uint32 Offset32 = 0x811c9dc5u;
		static constexpr uint32 Prime32 = 0x01000193u;
		static constexpr uint64 Offset64 = 0xcbf29ce484222325ull;
		static constexpr uint64 Prime64 = 0x00000100000001b3ull;

		[[nodiscard]] static uint32 MemFnv32(const void* InData, const uint64 Length, uint32 InOffset = Offset32)
		{
			const uint8* Data = static_cast<const uint8*>(InData);
			uint32 Hash = InOffset;
			for (uint64 Index = 0; Index < Length; ++Index)
			{
				Hash = (Hash ^ Data[Index]) * Prime32;
			}
			return Hash;
		}

		[[nodiscard]] static uint64 MemFnv64(const void* InData, const uint64 Length, uint64 InOffset = Offset64)
		{
			const uint8* Data = static_cast<const uint8*>(InData);
			uint64 Hash = InOffset;
			for (uint64 Index = 0; Index < Length; ++Index)
			{
				Hash = (Hash ^ Data[Index]) * Prime64;
			}
			return Hash;
		}
	};
}
