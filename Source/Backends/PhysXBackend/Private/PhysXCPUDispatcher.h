// PhysXCPUDispatcher.h - the CPU dispatchers of the PhysX backend (02_Architecture 8.2 "단일 스레드 dispatcher", 9.6,
// A.14). [MVP M5]   PT_BACKEND_NS=PhysX
//
// FPhysXCPUDispatcherSingleThread (UE4's T = 1 dispatcher, clean room from the design text): no worker thread, every
// task runs on the thread that submits it. PhysX submits follow-up tasks from inside run() and release() /
// removeReference(), so "run, then release" in submitTask would recurse once per link of the task chain (deep in a
// 10k-body scene). Instead each thread keeps a LIFO stack of pending tasks: a re-entrant submitTask only pushes and
// returns, and the outermost call pops and runs (run(), then release()) until the stack is empty. Nothing recurses: the
// submitTask nesting on a thread is at most 2 (the draining call and one re-entrant push), i.e. a re-entrancy depth of
// at most 1, which the dispatcher records (effective key physx.cpu_dispatcher). PhysX tasks never wait for each other
// inside run() (they may run on any worker in any order), so deferring a submitted task to the drain loop is valid; the
// result is cross-checked by state hash against PxDefaultCpuDispatcherCreate(0), which runs every task inline and
// recursively (ExtDefaultCpuDispatcher.cpp:109-114 [V]).
//
// FPhysXCPUDispatcherInline: PxDefaultCpuDispatcherCreate(0) behind a counting proxy (scene option
// CpuDispatcher=PxDefault at T = 1). PhysX hands follow-up tasks to the scene's dispatcher, i.e. to the proxy, so the
// proxy sees the inline dispatcher's recursion and records its depth - the negative control of the depth bound above.
//
// T >= 2: PxDefaultCpuDispatcherCreate(T) directly (T worker threads; the caller waits in fetchResults, 02 9.6).
#pragma once

#include "PhysXIncludes.h"
#include "PtTypes.h"

namespace PhysicsTest::PhysXBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	struct FPhysXDispatcherStats
	{
		uint64 NumTasks = 0;              // tasks run
		uint32 MaxReentrancyDepth = 0;    // deepest submitTask nesting minus 1 (0 = never re-entered)
		uint32 MaxPendingTasks = 0;       // deepest LIFO stack (single-thread dispatcher)
		uint32 NumGrowths = 0;            // stack reallocations (the stack keeps its high-water capacity)
		uint32 NumInlineFallbacks = 0;    // tasks run recursively because the stack could not grow (out of memory)
	};

	class FPhysXCPUDispatcherSingleThread final : public physx::PxCpuDispatcher
	{
	public:
		void submitTask(physx::PxBaseTask& Task) override;
		uint32_t getWorkerCount() const override { return 0; }

		const FPhysXDispatcherStats& GetStats() const { return Stats; }

	private:
		FPhysXDispatcherStats Stats;
	};

	class FPhysXCPUDispatcherInline final : public physx::PxCpuDispatcher
	{
	public:
		FPhysXCPUDispatcherInline();
		~FPhysXCPUDispatcherInline() override;
		FPhysXCPUDispatcherInline(const FPhysXCPUDispatcherInline&) = delete;
		FPhysXCPUDispatcherInline& operator=(const FPhysXCPUDispatcherInline&) = delete;

		void submitTask(physx::PxBaseTask& Task) override;
		uint32_t getWorkerCount() const override;
		bool IsValid() const { return Inner != nullptr; }

		const FPhysXDispatcherStats& GetStats() const { return Stats; }

	private:
		physx::PxDefaultCpuDispatcher* Inner = nullptr;
		uint32 Depth = 0;
		FPhysXDispatcherStats Stats;
	};

	// Frees this thread's pending-task stack (TermGamePhys, on the owner thread: the only thread that submits tasks to a
	// single-thread dispatcher).
	void ReleaseThreadTaskStack();
}
