// Sha256.cpp - SHA-256 per FIPS 180-4 (section 6.2).
#include "Hash/Sha256.h"

#include <cstring>

namespace PhysicsTest
{
	namespace
	{
		constexpr uint32 RoundConstants[64] = {
			0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
			0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
			0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
			0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
			0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
			0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
			0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
			0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
		};

		constexpr uint32 RotateRight(const uint32 Value, const uint32 Count)
		{
			return (Value >> Count) | (Value << (32u - Count));
		}
	}

	std::string FSHA256Digest::ToString() const
	{
		static constexpr char HexDigits[] = "0123456789abcdef";
		std::string Result;
		Result.reserve(64);
		for (const uint8 Byte : Hash)
		{
			Result.push_back(HexDigits[Byte >> 4]);
			Result.push_back(HexDigits[Byte & 0x0f]);
		}
		return Result;
	}

	bool FSHA256Digest::operator==(const FSHA256Digest& Other) const
	{
		return std::memcmp(Hash, Other.Hash, sizeof(Hash)) == 0;
	}

	void FSHA256::Reset()
	{
		State[0] = 0x6a09e667u;
		State[1] = 0xbb67ae85u;
		State[2] = 0x3c6ef372u;
		State[3] = 0xa54ff53au;
		State[4] = 0x510e527fu;
		State[5] = 0x9b05688cu;
		State[6] = 0x1f83d9abu;
		State[7] = 0x5be0cd19u;
		TotalBytes = 0;
		BufferBytes = 0;
	}

	void FSHA256::Transform(const uint8* Block)
	{
		uint32 Schedule[64];
		for (uint32 Index = 0; Index < 16; ++Index)
		{
			Schedule[Index] = (uint32(Block[Index * 4]) << 24) | (uint32(Block[Index * 4 + 1]) << 16)
				| (uint32(Block[Index * 4 + 2]) << 8) | uint32(Block[Index * 4 + 3]);
		}
		for (uint32 Index = 16; Index < 64; ++Index)
		{
			const uint32 S0 = RotateRight(Schedule[Index - 15], 7) ^ RotateRight(Schedule[Index - 15], 18) ^ (Schedule[Index - 15] >> 3);
			const uint32 S1 = RotateRight(Schedule[Index - 2], 17) ^ RotateRight(Schedule[Index - 2], 19) ^ (Schedule[Index - 2] >> 10);
			Schedule[Index] = Schedule[Index - 16] + S0 + Schedule[Index - 7] + S1;
		}

		uint32 A = State[0], B = State[1], C = State[2], D = State[3];
		uint32 E = State[4], F = State[5], G = State[6], H = State[7];
		for (uint32 Index = 0; Index < 64; ++Index)
		{
			const uint32 Sigma1 = RotateRight(E, 6) ^ RotateRight(E, 11) ^ RotateRight(E, 25);
			const uint32 Choose = (E & F) ^ (~E & G);
			const uint32 Temp1 = H + Sigma1 + Choose + RoundConstants[Index] + Schedule[Index];
			const uint32 Sigma0 = RotateRight(A, 2) ^ RotateRight(A, 13) ^ RotateRight(A, 22);
			const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
			const uint32 Temp2 = Sigma0 + Majority;
			H = G;
			G = F;
			F = E;
			E = D + Temp1;
			D = C;
			C = B;
			B = A;
			A = Temp1 + Temp2;
		}
		State[0] += A;
		State[1] += B;
		State[2] += C;
		State[3] += D;
		State[4] += E;
		State[5] += F;
		State[6] += G;
		State[7] += H;
	}

	void FSHA256::Update(const void* Data, uint64 Length)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		TotalBytes += Length;
		while (Length > 0)
		{
			const uint32 Take = static_cast<uint32>(Length < uint64(64u - BufferBytes) ? Length : uint64(64u - BufferBytes));
			std::memcpy(Buffer + BufferBytes, Bytes, Take);
			BufferBytes += Take;
			Bytes += Take;
			Length -= Take;
			if (BufferBytes == 64u)
			{
				Transform(Buffer);
				BufferBytes = 0;
			}
		}
	}

	FSHA256Digest FSHA256::Final()
	{
		const uint64 BitLength = TotalBytes * 8u;
		const uint8 Pad = 0x80u;
		Update(&Pad, 1);
		const uint8 Zero = 0;
		while (BufferBytes != 56u)
		{
			Update(&Zero, 1);
		}
		uint8 LengthBytes[8];
		for (uint32 Index = 0; Index < 8; ++Index)
		{
			LengthBytes[Index] = static_cast<uint8>(BitLength >> (56u - 8u * Index));
		}
		Update(LengthBytes, 8);

		FSHA256Digest Digest;
		for (uint32 Index = 0; Index < 8; ++Index)
		{
			Digest.Hash[Index * 4 + 0] = static_cast<uint8>(State[Index] >> 24);
			Digest.Hash[Index * 4 + 1] = static_cast<uint8>(State[Index] >> 16);
			Digest.Hash[Index * 4 + 2] = static_cast<uint8>(State[Index] >> 8);
			Digest.Hash[Index * 4 + 3] = static_cast<uint8>(State[Index]);
		}
		return Digest;
	}

	FSHA256Digest FSHA256::HashBuffer(const void* Data, const uint64 Length)
	{
		FSHA256 Hasher;
		Hasher.Update(Data, Length);
		return Hasher.Final();
	}
}
