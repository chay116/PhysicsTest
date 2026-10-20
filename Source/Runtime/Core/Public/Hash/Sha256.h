// Sha256.h - SHA-256 (FIPS 180-4) for scene/desc/result hashes (desc_sha256, scene_sha256, 02_Architecture 12).
#pragma once

#include "CoreTypes.h"

#include <string>
#include <string_view>

namespace PhysicsTest
{
	struct FSHA256Digest
	{
		uint8 Hash[32] = {};

		[[nodiscard]] std::string ToString() const;   // 64 lower-case hex characters
		[[nodiscard]] bool operator==(const FSHA256Digest& Other) const;
	};

	class FSHA256
	{
	public:
		FSHA256() { Reset(); }

		void Reset();
		void Update(const void* Data, uint64 Length);
		void Update(std::string_view Text) { Update(Text.data(), Text.size()); }
		// Finishes the hash; the object must be Reset() before reuse.
		[[nodiscard]] FSHA256Digest Final();

		[[nodiscard]] static FSHA256Digest HashBuffer(const void* Data, uint64 Length);
		[[nodiscard]] static std::string HashString(std::string_view Text) { return HashBuffer(Text.data(), Text.size()).ToString(); }

	private:
		void Transform(const uint8* Block);

		uint32 State[8] = {};
		uint8 Buffer[64] = {};
		uint64 TotalBytes = 0;
		uint32 BufferBytes = 0;
	};
}
