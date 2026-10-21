// PhysXCPUDispatcher.cpp - see PhysXCPUDispatcher.h.
#include "PhysXCPUDispatcher.h"

#include <stdlib.h>

namespace PhysicsTest::PhysXBackend
{
	namespace
	{
		// One per thread (POD: zero-initialised, no TLS constructor or destructor). Tasks holds Num pending tasks; Depth is
		// the number of submitTask calls of a single-thread dispatcher active on this thread.
		struct FTaskStack
		{
			physx::PxBaseTask** Tasks;
			uint32 Num;
			uint32 Capacity;
			uint32 Depth;
		};
		thread_local FTaskStack GTaskStack;

		bool Push(FTaskStack& Stack, physx::PxBaseTask& Task, FPhysXDispatcherStats& Stats)
		{
			if (Stack.Num == Stack.Capacity)
			{
				const uint32 NewCapacity = Stack.Capacity > 0 ? Stack.Capacity * 2u : 256u;
				physx::PxBaseTask** Grown = static_cast<physx::PxBaseTask**>(realloc(Stack.Tasks, static_cast<size_t>(NewCapacity) * sizeof(physx::PxBaseTask*)));
				if (Grown == nullptr)
				{
					return false;
				}
				Stack.Tasks = Grown;
				Stack.Capacity = NewCapacity;
				++Stats.NumGrowths;
			}
			Stack.Tasks[Stack.Num++] = &Task;
			Stats.MaxPendingTasks = Stack.Num > Stats.MaxPendingTasks ? Stack.Num : Stats.MaxPendingTasks;
			return true;
		}
	}

	void FPhysXCPUDispatcherSingleThread::submitTask(physx::PxBaseTask& Task)
	{
		FTaskStack& Stack = GTaskStack;
		++Stack.Depth;
		const uint32 Reentrancy = Stack.Depth - 1u;
		Stats.MaxReentrancyDepth = Reentrancy > Stats.MaxReentrancyDepth ? Reentrancy : Stats.MaxReentrancyDepth;
		if (!Push(Stack, Task, Stats))
		{
			// Out of memory for the stack: run this one task in place (the inline dispatcher's recursion), counted.
			++Stats.NumInlineFallbacks;
			Task.run();
			Task.release();
			++Stats.NumTasks;
			--Stack.Depth;
			return;
		}
		if (Stack.Depth > 1u)
		{
			// Re-entrant (a follow-up task released inside run() / release()): the outermost call runs it.
			--Stack.Depth;
			return;
		}
		while (Stack.Num > 0)
		{
			physx::PxBaseTask* Next = Stack.Tasks[--Stack.Num];
			Next->run();
			Next->release();
			++Stats.NumTasks;
		}
		--Stack.Depth;
	}

	void ReleaseThreadTaskStack()
	{
		FTaskStack& Stack = GTaskStack;
		free(Stack.Tasks);
		Stack.Tasks = nullptr;
		Stack.Num = 0;
		Stack.Capacity = 0;
		Stack.Depth = 0;
	}

	FPhysXCPUDispatcherInline::FPhysXCPUDispatcherInline()
		: Inner(physx::PxDefaultCpuDispatcherCreate(0))
	{
	}

	FPhysXCPUDispatcherInline::~FPhysXCPUDispatcherInline()
	{
		if (Inner != nullptr)
		{
			Inner->release();
		}
	}

	void FPhysXCPUDispatcherInline::submitTask(physx::PxBaseTask& Task)
	{
		// The inline dispatcher runs the task and releases it right here; a follow-up task comes back through this proxy
		// (the scene's dispatcher) one level deeper.
		++Depth;
		const uint32 Reentrancy = Depth - 1u;
		Stats.MaxReentrancyDepth = Reentrancy > Stats.MaxReentrancyDepth ? Reentrancy : Stats.MaxReentrancyDepth;
		++Stats.NumTasks;
		Inner->submitTask(Task);
		--Depth;
	}

	uint32_t FPhysXCPUDispatcherInline::getWorkerCount() const
	{
		return Inner != nullptr ? Inner->getWorkerCount() : 0u;
	}
}
