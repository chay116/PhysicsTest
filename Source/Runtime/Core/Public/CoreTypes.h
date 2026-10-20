// CoreTypes.h - fixed-width integer aliases and small helpers with UE names (UE: HAL/Platform.h, CoreTypes.h).
// Minimal implementation (DL18, S5): only what the harness needs, with UE spelling and semantics.
#pragma once

#include <cstddef>
#include <cstdint>

namespace PhysicsTest
{
	using int8 = std::int8_t;
	using int16 = std::int16_t;
	using int32 = std::int32_t;
	using int64 = std::int64_t;
	using uint8 = std::uint8_t;
	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;
	using uint64 = std::uint64_t;

	inline constexpr int32 INDEX_NONE = -1;
}

#define FORCEINLINE __forceinline
#define FORCENOINLINE __declspec(noinline)

// PT_DO_CHECK is build-global (UE: DO_CHECK) and comes from PT::Core's PUBLIC usage requirements
// (Source/Runtime/Core/CMakeLists.txt). A silent default here would let one TU expand check/ensure differently from
// the rest (ODR), so a missing definition is an error.
#ifndef PT_DO_CHECK
	#error "PT_DO_CHECK is not defined: link PT::Core (it carries PT_DO_CHECK=0|1 as a PUBLIC compile definition)"
#endif

// Core's macros use __VA_OPT__, which MSVC supports only with the conforming preprocessor.
#if defined(_MSC_VER) && (!defined(_MSVC_TRADITIONAL) || _MSVC_TRADITIONAL)
	#error "PT::Core needs the conforming preprocessor (/Zc:preprocessor, a PUBLIC compile option of PT::Core)"
#endif
