// AllocationHook.cpp - the counting hook of PhysicsTestBench.exe (03_WorkPlan M7: "test 빌드에서 타이밍 루프 안 할당이 0이다
// (카운팅 훅)", 02_Architecture 9.3). Replaces every form of the global operator new / delete of this image ([new.delete],
// replaceable allocation functions): the harness, the Kit, the static backend adapters and the SDK code that allocates
// through operator new all come here. The counters live in PT::BenchCore (FAllocationCounters); the bench reads them
// around each frame's Tick and records the totals in manifest.json (alloc group). The cost is one relaxed atomic increment
// per allocation, so the hook is the same in every run (headline runs included) and never changes what a run measures.
// Allocations an SDK makes through its own allocator (Jolt's JPH::Allocate -> malloc, PhysX's PxAllocatorCallback, Bullet's
// btAlignedAlloc) do not pass through operator new; every backend counts those itself and reports them in
// PtMemoryStats.NumAllocs - PhysX through its PxAllocatorCallback, Jolt through counting wrappers on the JPH::Allocate
// family of function pointers, Bullet through btAlignedAllocSetCustom (M7 fix round 0), Null its own arrays - and the
// manifest records the deltas next to these counters (alloc.backend_allocs_*). The hook binds inside this exe image only: a
// DLL backend's operator new is not seen (alloc.hook_scope). The positive control -InjectTickAllocation=<f> (M8, CTest
// PhysicsTest.Bench.AllocHookPositiveControl) shows that an operator new inside a Tick is counted.
#include "BenchCore.h"

#include <cstdlib>
#include <malloc.h>
#include <new>

namespace
{
	using PhysicsTest::FAllocationCounters;

	void* Allocate(const std::size_t Size)
	{
		FAllocationCounters::NumNew.fetch_add(1, std::memory_order_relaxed);
		return std::malloc(Size != 0 ? Size : 1);
	}

	void* AllocateAligned(const std::size_t Size, const std::align_val_t Alignment)
	{
		FAllocationCounters::NumNew.fetch_add(1, std::memory_order_relaxed);
		return _aligned_malloc(Size != 0 ? Size : 1, static_cast<std::size_t>(Alignment));
	}

	void Release(void* Pointer) noexcept
	{
		if (Pointer != nullptr)
		{
			FAllocationCounters::NumDelete.fetch_add(1, std::memory_order_relaxed);
			std::free(Pointer);
		}
	}

	void ReleaseAligned(void* Pointer) noexcept
	{
		if (Pointer != nullptr)
		{
			FAllocationCounters::NumDelete.fetch_add(1, std::memory_order_relaxed);
			_aligned_free(Pointer);
		}
	}

	struct FHookRegistration
	{
		FHookRegistration() { FAllocationCounters::bHookInstalled.store(true); }
	};
	const FHookRegistration GHookRegistration;
}

void* operator new(const std::size_t Size)
{
	if (void* Pointer = Allocate(Size))
	{
		return Pointer;
	}
	throw std::bad_alloc();
}

void* operator new[](const std::size_t Size)
{
	if (void* Pointer = Allocate(Size))
	{
		return Pointer;
	}
	throw std::bad_alloc();
}

void* operator new(const std::size_t Size, const std::nothrow_t&) noexcept
{
	return Allocate(Size);
}

void* operator new[](const std::size_t Size, const std::nothrow_t&) noexcept
{
	return Allocate(Size);
}

void* operator new(const std::size_t Size, const std::align_val_t Alignment)
{
	if (void* Pointer = AllocateAligned(Size, Alignment))
	{
		return Pointer;
	}
	throw std::bad_alloc();
}

void* operator new[](const std::size_t Size, const std::align_val_t Alignment)
{
	if (void* Pointer = AllocateAligned(Size, Alignment))
	{
		return Pointer;
	}
	throw std::bad_alloc();
}

void* operator new(const std::size_t Size, const std::align_val_t Alignment, const std::nothrow_t&) noexcept
{
	return AllocateAligned(Size, Alignment);
}

void* operator new[](const std::size_t Size, const std::align_val_t Alignment, const std::nothrow_t&) noexcept
{
	return AllocateAligned(Size, Alignment);
}

void operator delete(void* Pointer) noexcept
{
	Release(Pointer);
}

void operator delete[](void* Pointer) noexcept
{
	Release(Pointer);
}

void operator delete(void* Pointer, std::size_t) noexcept
{
	Release(Pointer);
}

void operator delete[](void* Pointer, std::size_t) noexcept
{
	Release(Pointer);
}

void operator delete(void* Pointer, const std::nothrow_t&) noexcept
{
	Release(Pointer);
}

void operator delete[](void* Pointer, const std::nothrow_t&) noexcept
{
	Release(Pointer);
}

void operator delete(void* Pointer, std::align_val_t) noexcept
{
	ReleaseAligned(Pointer);
}

void operator delete[](void* Pointer, std::align_val_t) noexcept
{
	ReleaseAligned(Pointer);
}

void operator delete(void* Pointer, std::size_t, std::align_val_t) noexcept
{
	ReleaseAligned(Pointer);
}

void operator delete[](void* Pointer, std::size_t, std::align_val_t) noexcept
{
	ReleaseAligned(Pointer);
}

void operator delete(void* Pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
	ReleaseAligned(Pointer);
}

void operator delete[](void* Pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
	ReleaseAligned(Pointer);
}
