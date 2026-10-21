// PhysXSupport.cpp - FPhysXAllocator and FPhysXErrorCallback (see PhysXSupport.h).
#include "PhysXSupport.h"

#include <malloc.h>
#include <string.h>

namespace PhysicsTest::PhysXBackend
{
	namespace
	{
		// The block header: the requested size, padded to keep the user pointer 16-byte aligned.
		constexpr size_t GHeaderBytes = 16;

		uint32 GetCodeIndex(const uint32 CodeBit)
		{
			uint32 Index = 0;
			while (Index < GPhysXNumErrorCodes && (CodeBit >> Index) != 1u)
			{
				++Index;
			}
			return Index;
		}
	}

	void* FPhysXAllocator::allocate(const size_t Size, const char* /*TypeName*/, const char* /*Filename*/, const int /*Line*/)
	{
		unsigned char* Block = static_cast<unsigned char*>(_aligned_malloc(Size + GHeaderBytes, 16));
		if (Block == nullptr)
		{
			return nullptr;   // PhysX reports eOUT_OF_MEMORY itself (the capacity error bit)
		}
		const uint64 Stored = static_cast<uint64>(Size);
		memcpy(Block, &Stored, sizeof(Stored));
		const int64 Live = LiveBytes.fetch_add(static_cast<int64>(Size), std::memory_order_relaxed) + static_cast<int64>(Size);
		int64 Peak = PeakBytes.load(std::memory_order_relaxed);
		while (Live > Peak && !PeakBytes.compare_exchange_weak(Peak, Live, std::memory_order_relaxed))
		{
		}
		NumAllocs.fetch_add(1, std::memory_order_relaxed);
		return Block + GHeaderBytes;
	}

	void FPhysXAllocator::deallocate(void* Ptr)
	{
		if (Ptr == nullptr)
		{
			return;
		}
		unsigned char* Block = static_cast<unsigned char*>(Ptr) - GHeaderBytes;
		uint64 Stored = 0;
		memcpy(&Stored, Block, sizeof(Stored));
		LiveBytes.fetch_sub(static_cast<int64>(Stored), std::memory_order_relaxed);
		NumFrees.fetch_add(1, std::memory_order_relaxed);
		_aligned_free(Block);
	}

	const char* GetErrorCodeName(const uint32 CodeBit)
	{
		switch (CodeBit)
		{
		case physx::PxErrorCode::eDEBUG_INFO: return "eDEBUG_INFO";
		case physx::PxErrorCode::eDEBUG_WARNING: return "eDEBUG_WARNING";
		case physx::PxErrorCode::eINVALID_PARAMETER: return "eINVALID_PARAMETER";
		case physx::PxErrorCode::eINVALID_OPERATION: return "eINVALID_OPERATION";
		case physx::PxErrorCode::eOUT_OF_MEMORY: return "eOUT_OF_MEMORY";
		case physx::PxErrorCode::eINTERNAL_ERROR: return "eINTERNAL_ERROR";
		case physx::PxErrorCode::eABORT: return "eABORT";
		case physx::PxErrorCode::ePERF_WARNING: return "ePERF_WARNING";
		default: return "?";
		}
	}

	void FPhysXErrorCallback::reportError(const physx::PxErrorCode::Enum Code, const char* Message, const char* File, const int Line)
	{
		const uint32 CodeBit = static_cast<uint32>(Code);
		const uint32 Index = GetCodeIndex(CodeBit);
		if (Index < GPhysXNumErrorCodes)
		{
			Counts[Index].fetch_add(1, std::memory_order_relaxed);
		}
		if (::PhysicsTest::Abi::FKitServices* Target = Kit.load(std::memory_order_acquire))
		{
			const uint32 Level = (CodeBit & GPhysXErrorBitsMask) == 0 ? PT_LOG_LOG
				: (Code == physx::PxErrorCode::eDEBUG_WARNING ? PT_LOG_WARNING : PT_LOG_ERROR);
			Target->LogToHost(Level, "PhysX", "%s %s(%d): %s", GetErrorCodeName(CodeBit), File != nullptr ? File : "?", Line, Message != nullptr ? Message : "");
		}
	}

	FPhysXErrorCounts FPhysXErrorCallback::GetCounts() const
	{
		FPhysXErrorCounts Out;
		for (uint32 Index = 0; Index < GPhysXNumErrorCodes; ++Index)
		{
			Out.Counts[Index] = Counts[Index].load(std::memory_order_relaxed);
		}
		return Out;
	}

	uint32 FPhysXErrorCallback::GetTotal() const
	{
		uint32 Total = 0;
		for (uint32 Index = 0; Index < GPhysXNumErrorCodes; ++Index)
		{
			Total += Counts[Index].load(std::memory_order_relaxed);
		}
		return Total;
	}

	uint32 GetNewErrorBits(const FPhysXErrorCounts& Before, const FPhysXErrorCounts& After)
	{
		uint32 Bits = 0;
		for (uint32 Index = 0; Index < GPhysXNumErrorCodes; ++Index)
		{
			if (After.Counts[Index] != Before.Counts[Index])
			{
				Bits |= 1u << Index;
			}
		}
		return Bits & GPhysXErrorBitsMask;
	}
}
