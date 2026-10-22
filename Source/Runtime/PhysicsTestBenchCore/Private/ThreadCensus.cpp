// ThreadCensus.cpp - the M10 part of BenchCore.h: the run's TSC calibration and FThreadCensus (02_Architecture 9.3, 12.2
// steps 5 / 11, 14.3 G10b; 03_WorkPlan M10 tasks 1-2). Read-only: this process's own threads and the CPUs' idle counters.
#include "BenchCore.h"

#include "Containers/StringConv.h"
#include "HAL/PlatformTime.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <iterator>
#include <cmath>

namespace PhysicsTest
{
	namespace
	{
		using FGetThreadDescription = HRESULT(WINAPI*)(HANDLE, PWSTR*);

		// GetThreadDescription exists from Windows 10 1607 on; looked up once instead of linked (a missing export would stop
		// the exe from loading on an older system).
		FGetThreadDescription GetThreadDescriptionFunction()
		{
			static const FGetThreadDescription Function = []
			{
				const HMODULE Kernel = GetModuleHandleW(L"kernel32.dll");
				return Kernel != nullptr ? reinterpret_cast<FGetThreadDescription>(reinterpret_cast<void*>(GetProcAddress(Kernel, "GetThreadDescription"))) : nullptr;
			}();
			return Function;
		}

		std::string ReadThreadName(const HANDLE Thread)
		{
			const FGetThreadDescription Describe = GetThreadDescriptionFunction();
			if (Describe == nullptr || Thread == nullptr)
			{
				return std::string();
			}
			PWSTR Text = nullptr;
			if (FAILED(Describe(Thread, &Text)) || Text == nullptr)
			{
				return std::string();
			}
			std::string Name = StringConv::WideToUtf8(Text);
			LocalFree(Text);
			return Name;
		}

		std::vector<uint64> ReadIdleCycles()
		{
			const uint32 Count = GetActiveProcessorCount(0);
			std::vector<uint64> Idle(Count, 0);
			ULONG Bytes = static_cast<ULONG>(Count * sizeof(ULONG64));
			if (!QueryIdleProcessorCycleTime(&Bytes, reinterpret_cast<PULONG64>(Idle.data())))
			{
				Idle.clear();
			}
			return Idle;
		}
	}

	// ---- the run's TSC calibration ------------------------------------------------------------------------------------
	FTscCalibration CalibrateTsc(const std::vector<FCycleCounter::FPair>& Pairs)
	{
		FTscCalibration Out;
		Out.Pairs = static_cast<uint32>(Pairs.size());
		if (Pairs.size() < 2)
		{
			return Out;
		}
		const FCycleCounter::FPair& First = Pairs.front();
		const FCycleCounter::FPair& Last = Pairs.back();
		Out.NsPerTick = FCycleCounter::CalibrateNsPerTick(First, Last);
		if (!(Out.NsPerTick > 0.0))
		{
			return Out;
		}
		Out.bValid = true;
		Out.TscHz = 1.0e9 / Out.NsPerTick;
		Out.SpanSeconds = static_cast<double>(Last.Qpc - First.Qpc) * FPlatformTime::GetSecondsPerCycle64();
		const double QpcSpan = static_cast<double>(Last.Qpc - First.Qpc);
		const double TscSpan = static_cast<double>(Last.Tsc - First.Tsc);
		double SumSquares = 0.0;
		for (const FCycleCounter::FPair& Pair : Pairs)
		{
			// Signed offsets: a pair taken before the first one (none here) would still be measured correctly.
			const double QpcFraction = (static_cast<double>(Pair.Qpc) - static_cast<double>(First.Qpc)) / QpcSpan;
			const double TscFraction = (static_cast<double>(Pair.Tsc) - static_cast<double>(First.Tsc)) / TscSpan;
			const double Residual = std::abs(QpcFraction - TscFraction);
			Out.MaxResidual = std::max(Out.MaxResidual, Residual);
			SumSquares += Residual * Residual;
			Out.MaxPairSpreadTicks = std::max(Out.MaxPairSpreadTicks, Pair.SpreadTicks);
		}
		Out.RmsResidual = std::sqrt(SumSquares / static_cast<double>(Pairs.size()));
		return Out;
	}

	// ---- FThreadCensus ------------------------------------------------------------------------------------------------
	FThreadCensus::~FThreadCensus()
	{
		CloseHandles();
	}

	FThreadCensus::FThreadCensus(FThreadCensus&& Other) noexcept
	{
		*this = std::move(Other);
	}

	FThreadCensus& FThreadCensus::operator=(FThreadCensus&& Other) noexcept
	{
		if (this != &Other)
		{
			CloseHandles();
			Threads = std::move(Other.Threads);
			WindowBeginReads = std::move(Other.WindowBeginReads);
			Handles = std::move(Other.Handles);
			Other.Handles.clear();
			MainThreadId = Other.MainThreadId;
			MainThreadCycles = Other.MainThreadCycles;
			ProcessCycles = Other.ProcessCycles;
			BracketCycles = Other.BracketCycles;
			Tsc = Other.Tsc;
			Qpc = Other.Qpc;
			IdleCycles = std::move(Other.IdleCycles);
			Error = std::move(Other.Error);
			bValid = Other.bValid;
		}
		return *this;
	}

	void FThreadCensus::CloseHandles()
	{
		for (void* Handle : Handles)
		{
			if (Handle != nullptr)
			{
				CloseHandle(static_cast<HANDLE>(Handle));
			}
		}
		Handles.clear();
	}

	uint64 FThreadCensus::SumHeldCycles() const
	{
		uint64 Sum = 0;
		for (size_t Index = 0; Index < Threads.size(); ++Index)
		{
			ULONG64 Cycles = 0;
			if (Handles[Index] != nullptr && !Threads[Index].bMain && QueryThreadCycleTime(static_cast<HANDLE>(Handles[Index]), &Cycles))
			{
				Sum += Cycles;
			}
		}
		return Sum;
	}

	uint64 FThreadCensus::MeasureBracketCycles() const
	{
		// The window's bracket with nothing inside (process, threads, main | main, threads, process), three times: the process
		// cycles no thread explains that the reads alone leave (QueryProcessCycleTime costs 10-100 us, and part of that is
		// charged to the process, not to the calling thread [V: M10 fix round 0 experiments, 0.05-0.4 M TSC cycles]). The median.
		uint64 Samples[3] = {};
		for (uint64& Sample : Samples)
		{
			ULONG64 ProcessA = 0;
			ULONG64 ProcessB = 0;
			ULONG64 MainA = 0;
			ULONG64 MainB = 0;
			QueryProcessCycleTime(GetCurrentProcess(), &ProcessA);
			const uint64 OthersA = SumHeldCycles();
			QueryThreadCycleTime(GetCurrentThread(), &MainA);
			QueryThreadCycleTime(GetCurrentThread(), &MainB);
			const uint64 OthersB = SumHeldCycles();
			QueryProcessCycleTime(GetCurrentProcess(), &ProcessB);
			const uint64 Own = (MainB - MainA) + (OthersB >= OthersA ? OthersB - OthersA : 0);
			const uint64 Process = ProcessB - ProcessA;
			Sample = Process > Own ? Process - Own : 0;
		}
		std::sort(std::begin(Samples), std::end(Samples));
		return Samples[1];
	}

	void FThreadCensus::ReadHeldHandles()
	{
		for (size_t Index = 0; Index < Threads.size(); ++Index)
		{
			if (Handles[Index] != nullptr && !Threads[Index].bMain)
			{
				ULONG64 Cycles = 0;
				Threads[Index].bValid = QueryThreadCycleTime(static_cast<HANDLE>(Handles[Index]), &Cycles) != FALSE;
				Threads[Index].Cycles = Cycles;
			}
		}
	}

	bool FThreadCensus::Capture(const ETiming Timing, const FThreadCensus* WindowBegin)
	{
		CloseHandles();
		Threads.clear();
		WindowBeginReads.clear();
		Error.clear();
		bValid = false;
		MainThreadId = GetCurrentThreadId();
		const auto ReadProcess = [this]
		{
			ULONG64 Process = 0;
			QueryProcessCycleTime(GetCurrentProcess(), &Process);
			ProcessCycles = Process;
		};
		const auto ReadMainAndClocks = [this](const bool bClocksFirst)
		{
			ULONG64 Main = 0;
			if (bClocksFirst)
			{
				Tsc = FCycleCounter::Read();
				Qpc = FPlatformTime::Cycles64();
				QueryThreadCycleTime(GetCurrentThread(), &Main);
			}
			else
			{
				QueryThreadCycleTime(GetCurrentThread(), &Main);
				Qpc = FPlatformTime::Cycles64();
				Tsc = FCycleCounter::Read();
			}
			MainThreadCycles = Main;
		};
		// M10 fix round 0: the counters of a window bound are read together, outside the enumeration, nested so that the process
		// total's window encloses the threads' and theirs encloses the main thread's and the clocks' (microseconds each):
		//   census #1 (CountersLast): enumerate -> idle -> bracket baseline -> process, its threads (held handles), main, QPC, TSC
		//                             = the window begins
		//   census #2 (CountersFirst): TSC, QPC, main = the window ends -> census #1's threads (its handles), process -> idle ->
		//                              enumerate (only a thread born inside the window is read here, a few ms late)
		// The idle read stays outside the process bracket: it costs about 12 us, and roughly 0.3 M cycles of process time that no
		// thread carries appear with each one read inside it [V: M10 fix round 0 experiments].
		BracketCycles = 0;
		if (Timing == ETiming::CountersFirst)
		{
			ReadMainAndClocks(true);
			if (WindowBegin != nullptr)
			{
				WindowBeginReads = WindowBegin->Threads;
				for (FCensusThread& Thread : WindowBeginReads)
				{
					if (!Thread.bMain)
					{
						uint64 Cycles = 0;
						Thread.bValid = WindowBegin->ReadHeldThreadCycles(Thread.ThreadId, Cycles);
						Thread.Cycles = Cycles;
					}
				}
			}
			ReadProcess();
			IdleCycles = ReadIdleCycles();
		}
		const HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (Snapshot == INVALID_HANDLE_VALUE)
		{
			Error = "CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD) failed (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		const DWORD Pid = GetCurrentProcessId();
		THREADENTRY32 Entry = {};
		Entry.dwSize = sizeof(Entry);
		for (BOOL bMore = Thread32First(Snapshot, &Entry); bMore; bMore = Thread32Next(Snapshot, &Entry))
		{
			if (Entry.th32OwnerProcessID != Pid)
			{
				continue;
			}
			FCensusThread Thread;
			Thread.ThreadId = Entry.th32ThreadID;
			Thread.bMain = Entry.th32ThreadID == MainThreadId;
			const HANDLE Handle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, Entry.th32ThreadID);
			if (Handle != nullptr)
			{
				ULONG64 Cycles = 0;
				Thread.bValid = QueryThreadCycleTime(Handle, &Cycles) != FALSE;
				Thread.Cycles = Cycles;
				Thread.Name = ReadThreadName(Handle);
			}
			Threads.push_back(std::move(Thread));
			Handles.push_back(Handle);
		}
		CloseHandle(Snapshot);
		if (Timing == ETiming::CountersLast)
		{
			IdleCycles = ReadIdleCycles();
			BracketCycles = MeasureBracketCycles();
			ReadProcess();
			ReadHeldHandles();   // every enumerated thread again, now: the window's begin, not the enumeration's
			ReadMainAndClocks(false);
		}
		// The main thread's entry carries the value read with the clocks (exact, read by the thread itself).
		for (FCensusThread& Thread : Threads)
		{
			if (Thread.bMain)
			{
				Thread.Cycles = MainThreadCycles;
				Thread.bValid = true;
			}
		}
		bValid = !IdleCycles.empty();
		if (!bValid)
		{
			Error = "QueryIdleProcessorCycleTime failed";
		}
		return bValid;
	}

	bool FThreadCensus::ReadHeldThreadCycles(const uint32 ThreadId, uint64& OutCycles) const
	{
		for (size_t Index = 0; Index < Threads.size(); ++Index)
		{
			if (Threads[Index].ThreadId == ThreadId && Handles[Index] != nullptr)
			{
				ULONG64 Cycles = 0;
				if (QueryThreadCycleTime(static_cast<HANDLE>(Handles[Index]), &Cycles))
				{
					OutCycles = Cycles;
					return true;
				}
				return false;
			}
		}
		return false;
	}

	FCensusWindow MeasureCensusWindow(const FThreadCensus& Begin, const FThreadCensus& End, const uint64 Mask, const uint32 Threads)
	{
		FCensusWindow Out;
		Out.Threads = Threads > 0 ? Threads : 1;
		Out.Mask = Mask;
		if (!Begin.IsValid() || !End.IsValid())
		{
			Out.Error = !Begin.IsValid() ? "census #1 (window begin): " + Begin.GetError() : "census #2 (window end): " + End.GetError();
			return Out;
		}
		if (Begin.GetMainThreadId() != End.GetMainThreadId() || End.GetTsc() <= Begin.GetTsc())
		{
			Out.Error = "the two censuses were not taken by one thread in order";
			return Out;
		}
		Out.WallTsc = End.GetTsc() - Begin.GetTsc();
		Out.WallSeconds = static_cast<double>(End.GetQpc() - Begin.GetQpc()) * FPlatformTime::GetSecondsPerCycle64();
		Out.MainCycles = End.GetMainThreadCycles() - Begin.GetMainThreadCycles();
		Out.ProcessCycles = End.GetProcessCycles() >= Begin.GetProcessCycles() ? End.GetProcessCycles() - Begin.GetProcessCycles() : 0;
		Out.ThreadsBegin = static_cast<uint32>(Begin.GetThreads().size());
		Out.ThreadsEnd = static_cast<uint32>(End.GetThreads().size());
		FCensusThreadDelta Main;
		Main.ThreadId = Begin.GetMainThreadId();
		Main.Cycles = Out.MainCycles;
		Main.bMain = true;
		std::vector<FCensusThreadDelta> Others;
		// Threads of the Begin census: their cycles at the End census's counters (read there through census #1's handles, alive
		// or exited; M10 fix round 0), else - an End taken without its Begin - the End enumeration's value or, for an exited
		// thread, its final value through the held handle.
		const std::vector<FCensusThread>& AtEnd = End.GetWindowBeginReads();
		for (const FCensusThread& Thread : Begin.GetThreads())
		{
			if (Thread.bMain)
			{
				Main.Name = Thread.Name;
				continue;
			}
			if (!Thread.bValid)
			{
				continue;
			}
			uint64 Now = 0;
			bool bRead = false;
			const auto Read = std::find_if(AtEnd.begin(), AtEnd.end(), [&Thread](const FCensusThread& Other) { return Other.ThreadId == Thread.ThreadId; });
			if (Read != AtEnd.end())
			{
				bRead = Read->bValid;
				Now = Read->Cycles;
			}
			else
			{
				const auto Enumerated = std::find_if(End.GetThreads().begin(), End.GetThreads().end(), [&Thread](const FCensusThread& Other) { return Other.ThreadId == Thread.ThreadId; });
				bRead = Enumerated != End.GetThreads().end() ? Enumerated->bValid : Begin.ReadHeldThreadCycles(Thread.ThreadId, Now);
				Now = Enumerated != End.GetThreads().end() ? Enumerated->Cycles : Now;
			}
			if (!bRead)
			{
				continue;
			}
			FCensusThreadDelta Delta;
			Delta.ThreadId = Thread.ThreadId;
			Delta.Name = Thread.Name;
			Delta.Cycles = Now >= Thread.Cycles ? Now - Thread.Cycles : 0;
			Delta.bExited = std::none_of(End.GetThreads().begin(), End.GetThreads().end(), [&Thread](const FCensusThread& Other) { return Other.ThreadId == Thread.ThreadId; });
			Others.push_back(std::move(Delta));
		}
		// Threads born inside the window: every cycle they used is the window's.
		for (const FCensusThread& Thread : End.GetThreads())
		{
			if (Thread.bMain || !Thread.bValid)
			{
				continue;
			}
			const bool bKnown = std::any_of(Begin.GetThreads().begin(), Begin.GetThreads().end(), [&Thread](const FCensusThread& Other) { return Other.ThreadId == Thread.ThreadId; });
			if (!bKnown)
			{
				FCensusThreadDelta Delta;
				Delta.ThreadId = Thread.ThreadId;
				Delta.Name = Thread.Name;
				Delta.Cycles = Thread.Cycles;
				Delta.bNew = true;
				Others.push_back(std::move(Delta));
			}
		}
		std::sort(Others.begin(), Others.end(), [](const FCensusThreadDelta& A, const FCensusThreadDelta& B) { return A.Cycles != B.Cycles ? A.Cycles > B.Cycles : A.ThreadId < B.ThreadId; });
		for (const FCensusThreadDelta& Delta : Others)
		{
			Out.ForeignCycles += Delta.Cycles;
		}
		Out.PerThread.push_back(std::move(Main));
		Out.PerThread.insert(Out.PerThread.end(), Others.begin(), Others.end());
		Out.OwnCycles = Out.MainCycles + Out.ForeignCycles;
		Out.UnattributedCycles = Out.ProcessCycles > Out.OwnCycles ? Out.ProcessCycles - Out.OwnCycles : 0;
		Out.BracketCycles = Begin.GetBracketCycles();
		const uint64 Net = Out.UnattributedCycles > Out.BracketCycles ? Out.UnattributedCycles - Out.BracketCycles : 0;
		Out.UnattributedFraction = Out.OwnCycles > 0 ? static_cast<double>(Net) / static_cast<double>(Out.OwnCycles) : 0.0;
		// The mask's CPUs: elapsed minus idle (other processes, interrupts and DPCs, and this process).
		const std::vector<uint64>& IdleBegin = Begin.GetIdleCycles();
		const std::vector<uint64>& IdleEnd = End.GetIdleCycles();
		for (size_t Cpu = 0; Cpu < IdleBegin.size() && Cpu < IdleEnd.size() && Cpu < 64; ++Cpu)
		{
			if (((Mask >> Cpu) & 1ull) == 0)
			{
				continue;
			}
			const uint64 Idle = IdleEnd[Cpu] >= IdleBegin[Cpu] ? IdleEnd[Cpu] - IdleBegin[Cpu] : 0;
			Out.MaskBusyCycles += Idle < Out.WallTsc ? Out.WallTsc - Idle : 0;
		}
		Out.OtherOnMaskCycles = Out.MaskBusyCycles > Out.OwnCycles ? Out.MaskBusyCycles - Out.OwnCycles : 0;
		const double Capacity = static_cast<double>(Out.WallTsc) * static_cast<double>(Out.Threads);
		const double Available = Capacity - static_cast<double>(Out.OtherOnMaskCycles);
		Out.CpuEffRaw = Capacity > 0.0 ? static_cast<double>(Out.OwnCycles) / Capacity : 0.0;
		Out.CpuEff = Available > 0.0 ? static_cast<double>(Out.OwnCycles) / Available : 0.0;
		Out.ForeignFraction = Out.OwnCycles > 0 ? static_cast<double>(Out.ForeignCycles) / static_cast<double>(Out.OwnCycles) : 0.0;
		Out.bValid = true;
		return Out;
	}
}
