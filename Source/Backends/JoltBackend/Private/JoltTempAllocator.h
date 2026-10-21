// JoltTempAllocator.h - the per-scene Jolt temp allocator with an overflow record (02_Architecture 8.3, 9.4 G12). [MVP M3]
// Jolt's TempAllocatorImpl aborts the process when its block is too small (TempAllocator.h: std::abort). The harness
// sizes the block from the scenario's capacity hints instead and must turn an overflow into a failed run, not a crash
// (02 9.4: "넘치면 run을 실패로 처리한다", G12). This allocator serves from one pre-allocated block (LIFO, like
// TempAllocatorImpl), falls back to the heap when the block is full and counts every fallback; the scene reports a
// fallback as an engine error bit and in the effective settings, and records the peak block usage.
#pragma once

#include "JoltIncludes.h"
#include "PtTypes.h"

namespace PhysicsTest::JoltBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	class FJoltTempAllocator final : public JPH::TempAllocator
	{
	public:
		explicit FJoltTempAllocator(const size_t InSize)
			: Size(InSize)
		{
			Base = static_cast<uint8*>(JPH::AlignedAllocate(Size, JPH_RVECTOR_ALIGNMENT));
		}
		~FJoltTempAllocator() override
		{
			JPH::AlignedFree(Base);
		}
		FJoltTempAllocator(const FJoltTempAllocator&) = delete;
		FJoltTempAllocator& operator=(const FJoltTempAllocator&) = delete;

		void* Allocate(const JPH::uint InSize) override
		{
			if (InSize == 0)
			{
				return nullptr;
			}
			const size_t Aligned = JPH::AlignUp(static_cast<size_t>(InSize), JPH_RVECTOR_ALIGNMENT);
			if (Base != nullptr && Top + Aligned <= Size)
			{
				void* Address = Base + Top;
				Top += Aligned;
				Peak = Top > Peak ? Top : Peak;
				return Address;
			}
			++NumFallbacks;
			FallbackBytes += InSize;
			return JPH::AlignedAllocate(InSize, JPH_RVECTOR_ALIGNMENT);
		}

		void Free(void* InAddress, const JPH::uint InSize) override
		{
			if (InAddress == nullptr)
			{
				return;
			}
			if (Base != nullptr && InAddress >= Base && InAddress < Base + Size)
			{
				Top -= JPH::AlignUp(static_cast<size_t>(InSize), JPH_RVECTOR_ALIGNMENT);   // LIFO, as TempAllocatorImpl
				return;
			}
			JPH::AlignedFree(InAddress);
		}

		// Touches every page of the block once (one write per 4 KiB page), so that the first steps do not take the page
		// faults inside the timed T_step as Jolt's usage grows (02 9.5: PrepareForSimulation is the untimed opportunity).
		// Returns the number of pages touched. The block's content is scratch: the zero bytes written mean nothing.
		size_t PreFault()
		{
			constexpr size_t PageBytes = 4096;
			volatile uint8* const Bytes = Base;
			size_t Pages = 0;
			for (size_t Offset = 0; Base != nullptr && Offset < Size; Offset += PageBytes)
			{
				Bytes[Offset] = 0;
				++Pages;
			}
			return Pages;
		}

		size_t GetSize() const { return Size; }
		size_t GetPeakUsage() const { return Peak; }
		uint64 GetNumFallbacks() const { return NumFallbacks; }
		uint64 GetFallbackBytes() const { return FallbackBytes; }

	private:
		uint8* Base = nullptr;
		size_t Size = 0;
		size_t Top = 0;
		size_t Peak = 0;
		uint64 NumFallbacks = 0;
		uint64 FallbackBytes = 0;
	};
}
