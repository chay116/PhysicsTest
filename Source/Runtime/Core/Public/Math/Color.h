// Color.h - FColor (8-bit sRGB colour). UE: Core/Public/Math/Color.h. [MVP M4, debug draw palette]
// Memory layout as UE on little-endian Windows: B, G, R, A (Color.h union { struct { uint8 B, G, R, A; } }), so the packed
// 32-bit value is ARGB (UE DWColor / ToPackedARGB). The named colours are UE's values (Color.cpp:26-39 [V]).
// Not mirrored: FLinearColor and the colour arithmetic (the debug draw uses ColorScale = 1 everywhere).
#pragma once

#include "CoreTypes.h"

namespace PhysicsTest
{
	struct FColor
	{
		uint8 B = 0;
		uint8 G = 0;
		uint8 R = 0;
		uint8 A = 0;

		static const FColor White;
		static const FColor Black;
		static const FColor Transparent;
		static const FColor Red;
		static const FColor Green;
		static const FColor Blue;
		static const FColor Yellow;
		static const FColor Cyan;
		static const FColor Magenta;
		static const FColor Orange;
		static const FColor Purple;
		static const FColor Turquoise;
		static const FColor Silver;
		static const FColor Emerald;

		constexpr FColor() = default;
		constexpr FColor(const uint8 InR, const uint8 InG, const uint8 InB, const uint8 InA = 255) : B(InB), G(InG), R(InR), A(InA) {}

		[[nodiscard]] constexpr bool operator==(const FColor& Other) const { return R == Other.R && G == Other.G && B == Other.B && A == Other.A; }
		[[nodiscard]] constexpr bool operator!=(const FColor& Other) const { return !(*this == Other); }

		// UE DWColor(): A << 24 | R << 16 | G << 8 | B (also the PtDebugDrawCmd::ColorBGRA value: bytes B, G, R, A).
		[[nodiscard]] constexpr uint32 ToPackedARGB() const
		{
			return (static_cast<uint32>(A) << 24) | (static_cast<uint32>(R) << 16) | (static_cast<uint32>(G) << 8) | static_cast<uint32>(B);
		}
		// R in the lowest byte: the byte order of DXGI_FORMAT_R8G8B8A8_UNORM.
		[[nodiscard]] constexpr uint32 ToPackedABGR() const
		{
			return (static_cast<uint32>(A) << 24) | (static_cast<uint32>(B) << 16) | (static_cast<uint32>(G) << 8) | static_cast<uint32>(R);
		}
		[[nodiscard]] static constexpr FColor FromPackedARGB(const uint32 Value)
		{
			return FColor(static_cast<uint8>((Value >> 16) & 0xFF), static_cast<uint8>((Value >> 8) & 0xFF), static_cast<uint8>(Value & 0xFF),
				static_cast<uint8>((Value >> 24) & 0xFF));
		}
	};

	inline const FColor FColor::White(255, 255, 255);
	inline const FColor FColor::Black(0, 0, 0);
	inline const FColor FColor::Transparent(0, 0, 0, 0);
	inline const FColor FColor::Red(255, 0, 0);
	inline const FColor FColor::Green(0, 255, 0);
	inline const FColor FColor::Blue(0, 0, 255);
	inline const FColor FColor::Yellow(255, 255, 0);
	inline const FColor FColor::Cyan(0, 255, 255);
	inline const FColor FColor::Magenta(255, 0, 255);
	inline const FColor FColor::Orange(243, 156, 18);
	inline const FColor FColor::Purple(169, 7, 228);
	inline const FColor FColor::Turquoise(26, 188, 156);
	inline const FColor FColor::Silver(189, 195, 199);
	inline const FColor FColor::Emerald(46, 204, 113);
}
