// PtTypes.h - fixed-width aliases so backend code (e.g. the UE4-named PhysX clean room) never needs harness Core
// (02_Architecture 7.3, 8.1, A.4). Definitions only: no PT_BACKEND_NS needed.
#pragma once

#include <cstddef>
#include <cstdint>

namespace PhysicsTest::Abi::FixedWidth
{
	using int8 = std::int8_t;
	using int16 = std::int16_t;
	using int32 = std::int32_t;
	using int64 = std::int64_t;
	using uint8 = std::uint8_t;
	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;
	using uint64 = std::uint64_t;
}

namespace PhysicsTest::Abi
{
	using namespace FixedWidth;
}
