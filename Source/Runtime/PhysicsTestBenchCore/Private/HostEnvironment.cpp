// HostEnvironment.cpp - the M7 part of BenchCore.h: processor topology, FHostEnvironment, FHostLoadMonitor,
// FProcessSpawner, FAllocationCounters and the clock read overhead (02_Architecture 9.3, 9.5, 9.6, 12.2; 03_WorkPlan M7).
// Every control is per process. Nothing here changes a system setting: the power scheme is only read (BenchCore.cpp).
#include "BenchCore.h"

#include "Containers/StringConv.h"
#include "HAL/PlatformTime.h"

#include <Windows.h>
#include <intrin.h>
#include <powrprof.h>
#include <timeapi.h>   // timeBeginPeriod (winmm)

#include <algorithm>
#include <bit>
#include <chrono>
#include <thread>

namespace PhysicsTest
{
	std::atomic<uint64> FAllocationCounters::NumNew{ 0 };
	std::atomic<uint64> FAllocationCounters::NumDelete{ 0 };
	std::atomic<bool> FAllocationCounters::bHookInstalled{ false };

	namespace
	{
		// CallNtPowerInformation(ProcessorInformation) output; the SDK documents but does not declare it (powerbase.h).
		struct FProcessorPowerInformation
		{
			ULONG Number;
			ULONG MaxMhz;
			ULONG CurrentMhz;
			ULONG MhzLimit;
			ULONG MaxIdleState;
			ULONG CurrentIdleState;
		};

		using FNtQueryTimerResolution = LONG(NTAPI*)(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution);

		// Spins until the QPC deadline (no sleep, no yield: a busy core).
		void SpinUntil(const uint64 DeadlineTicks)
		{
			volatile uint64 Sink = 0;
			while (FPlatformTime::Cycles64() < DeadlineTicks)
			{
				for (int Index = 0; Index < 256; ++Index)
				{
					Sink = Sink + static_cast<uint64>(Index);
				}
			}
		}

		// One spinning thread per CPU of Mask: the calling thread takes the lowest CPU (pinned for the duration, its affinity
		// restored afterwards), helper threads the others.
		void SpinCpus(const double Seconds, const uint64 Mask)
		{
			if (Seconds <= 0.0 || Mask == 0)
			{
				return;
			}
			const std::vector<uint32> Cpus = MaskToCpus(Mask);
			const uint64 Deadline = FPlatformTime::Cycles64() + static_cast<uint64>(Seconds * static_cast<double>(FPlatformTime::GetCyclesPerSecond64()));
			std::vector<std::thread> Helpers;
			Helpers.reserve(Cpus.size());
			for (size_t Index = 1; Index < Cpus.size(); ++Index)
			{
				const uint64 CpuMask = 1ull << Cpus[Index];
				Helpers.emplace_back([CpuMask, Deadline]
				{
					SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(CpuMask));
					SpinUntil(Deadline);
				});
			}
			const DWORD_PTR Previous = SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(1ull << Cpus.front()));
			SpinUntil(Deadline);
			if (Previous != 0)
			{
				SetThreadAffinityMask(GetCurrentThread(), Previous);
			}
			for (std::thread& Helper : Helpers)
			{
				Helper.join();
			}
		}

		uint32 GetGroup0ProcessorCount()
		{
			return GetActiveProcessorCount(0);
		}
	}

	// ---- topology -------------------------------------------------------------------------------------------------------
	uint32 CountBits(const uint64 Mask)
	{
		return static_cast<uint32>(std::popcount(Mask));
	}

	std::vector<uint32> MaskToCpus(const uint64 Mask)
	{
		std::vector<uint32> Cpus;
		for (uint32 Cpu = 0; Cpu < 64; ++Cpu)
		{
			if ((Mask >> Cpu) & 1ull)
			{
				Cpus.push_back(Cpu);
			}
		}
		return Cpus;
	}

	uint64 FProcessorTopology::CoresOf(const uint64 Mask) const
	{
		uint64 Out = 0;
		for (const FProcessorCore& Core : Cores)
		{
			if ((Core.LogicalMask & Mask) != 0)
			{
				Out |= Core.LogicalMask;
			}
		}
		return Out;
	}

	uint64 FProcessorTopology::MakeThreadMask(const uint32 Threads) const
	{
		if (Threads == 0 || Threads > PCoreFirstLogicals.size())
		{
			return 0;
		}
		uint64 Mask = 0;
		for (uint32 Index = 0; Index < Threads; ++Index)
		{
			Mask |= 1ull << PCoreFirstLogicals[Index];
		}
		return Mask;
	}

	FProcessorTopology::FMaskShape FProcessorTopology::DescribeMask(const uint64 Mask) const
	{
		FMaskShape Shape;
		for (const FProcessorCore& Core : Cores)
		{
			const uint64 Used = Core.LogicalMask & Mask;
			if (Used == 0)
			{
				continue;
			}
			++Shape.Cores;
			Shape.SmtPairCores += CountBits(Used) > 1 ? 1u : 0u;
			Shape.ECores += Core.EfficiencyClass < MaxEfficiencyClass ? 1u : 0u;
		}
		Shape.Series = Shape.ECores > 0 ? "with_e_cores" : (Shape.SmtPairCores > 0 ? "p_cores_smt" : "p_cores_one_lp");
		return Shape;
	}

	FProcessorTopology ReadProcessorTopology()
	{
		FProcessorTopology Topology;
		DWORD Length = 0;
		GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &Length);
		if (Length == 0)
		{
			return Topology;
		}
		std::vector<uint8> Buffer(Length);
		if (!GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(Buffer.data()), &Length))
		{
			return Topology;
		}
		for (DWORD Offset = 0; Offset < Length;)
		{
			const auto* Info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(Buffer.data() + Offset);
			if (Info->Relationship == RelationProcessorCore)
			{
				for (WORD Group = 0; Group < Info->Processor.GroupCount; ++Group)
				{
					const GROUP_AFFINITY& Affinity = Info->Processor.GroupMask[Group];
					if (Affinity.Group != 0 || Affinity.Mask == 0)
					{
						continue;   // the harness runs in processor group 0 (<= 64 logical CPUs)
					}
					FProcessorCore Core;
					Core.LogicalMask = static_cast<uint64>(Affinity.Mask);
					Core.FirstLogical = static_cast<uint32>(std::countr_zero(Core.LogicalMask));
					Core.EfficiencyClass = Info->Processor.EfficiencyClass;
					Topology.Cores.push_back(Core);
					Topology.SystemMask |= Core.LogicalMask;
					Topology.MaxEfficiencyClass = std::max(Topology.MaxEfficiencyClass, Core.EfficiencyClass);
				}
			}
			Offset += Info->Size;
		}
		std::sort(Topology.Cores.begin(), Topology.Cores.end(), [](const FProcessorCore& A, const FProcessorCore& B) { return A.FirstLogical < B.FirstLogical; });
		for (const FProcessorCore& Core : Topology.Cores)
		{
			if (Core.EfficiencyClass == Topology.MaxEfficiencyClass)
			{
				Topology.PCoreFirstLogicals.push_back(Core.FirstLogical);
			}
		}
		return Topology;
	}

	// ---- FHostEnvironment -----------------------------------------------------------------------------------------------
	bool FHostEnvironment::ApplyAffinity(const uint64 Mask, std::string& OutError)
	{
		if (!SetProcessAffinityMask(GetCurrentProcess(), static_cast<DWORD_PTR>(Mask)))
		{
			OutError = "SetProcessAffinityMask failed (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		return true;
	}

	bool FHostEnvironment::SetHighPriority(std::string& OutError)
	{
		if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
		{
			OutError = "SetPriorityClass(HIGH_PRIORITY_CLASS) failed (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		return true;
	}

	bool FHostEnvironment::DisablePowerThrottling(std::string& OutError)
	{
		PROCESS_POWER_THROTTLING_STATE State = {};
		State.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
		State.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
		State.StateMask = 0;   // controlled and off: no EcoQoS, timer requests honoured
		if (!SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &State, sizeof(State)))
		{
			OutError = "SetProcessInformation(ProcessPowerThrottling) failed (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		return true;
	}

	bool FHostEnvironment::SetTimerResolution1ms(std::string& OutError)
	{
		if (timeBeginPeriod(1) != TIMERR_NOERROR)
		{
			OutError = "timeBeginPeriod(1) failed";
			return false;
		}
		return true;
	}

	FTimerResolution FHostEnvironment::QueryTimerResolution()
	{
		FTimerResolution Out;
		if (const HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll"))
		{
			if (const auto Query = reinterpret_cast<FNtQueryTimerResolution>(reinterpret_cast<void*>(GetProcAddress(Ntdll, "NtQueryTimerResolution"))))
			{
				ULONG Coarsest = 0;
				ULONG Finest = 0;
				ULONG Current = 0;
				if (Query(&Coarsest, &Finest, &Current) >= 0)
				{
					Out.bValid = true;
					Out.CoarsestMs = static_cast<double>(Coarsest) * 1.0e-4;   // 100 ns units
					Out.FinestMs = static_cast<double>(Finest) * 1.0e-4;
					Out.CurrentMs = static_cast<double>(Current) * 1.0e-4;
				}
			}
		}
		return Out;
	}

	std::vector<FCpuFrequency> FHostEnvironment::ReadCpuFrequencies(const uint64 Mask)
	{
		std::vector<FCpuFrequency> Out;
		const uint32 Count = GetGroup0ProcessorCount();
		std::vector<FProcessorPowerInformation> Info(Count);
		if (CallNtPowerInformation(ProcessorInformation, nullptr, 0, Info.data(), static_cast<ULONG>(Info.size() * sizeof(FProcessorPowerInformation))) != 0)
		{
			return Out;
		}
		for (const FProcessorPowerInformation& Cpu : Info)
		{
			if (Cpu.Number < 64 && ((Mask >> Cpu.Number) & 1ull))
			{
				Out.push_back(FCpuFrequency{ Cpu.Number, Cpu.MaxMhz, Cpu.CurrentMhz, Cpu.MhzLimit });
			}
		}
		return Out;
	}

	std::string FHostEnvironment::GetPriorityClassName()
	{
		switch (GetPriorityClass(GetCurrentProcess()))
		{
		case IDLE_PRIORITY_CLASS: return "IDLE";
		case BELOW_NORMAL_PRIORITY_CLASS: return "BELOW_NORMAL";
		case NORMAL_PRIORITY_CLASS: return "NORMAL";
		case ABOVE_NORMAL_PRIORITY_CLASS: return "ABOVE_NORMAL";
		case HIGH_PRIORITY_CLASS: return "HIGH";
		case REALTIME_PRIORITY_CLASS: return "REALTIME";
		default: return "?";
		}
	}

	void FHostEnvironment::SpinPreRoll(const double Seconds, const uint64 Mask)
	{
		SpinCpus(Seconds, Mask);
	}

	void FHostEnvironment::RunBusyLoad(const double Seconds, const uint64 Mask)
	{
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
		SpinCpus(Seconds, Mask);
	}

	// ---- FHostLoadMonitor -----------------------------------------------------------------------------------------------
	void FHostLoadMonitor::Begin()
	{
		ULONG Bytes = static_cast<ULONG>(GetGroup0ProcessorCount() * sizeof(ULONG64));
		IdleCyclesBegin.assign(GetGroup0ProcessorCount(), 0);
		ULONG64 ProcessCycles = 0;
		bStarted = QueryIdleProcessorCycleTime(&Bytes, reinterpret_cast<PULONG64>(IdleCyclesBegin.data())) != FALSE
			&& QueryProcessCycleTime(GetCurrentProcess(), &ProcessCycles) != FALSE;
		ProcessCyclesBegin = ProcessCycles;
		TscBegin = __rdtsc();
		QpcBegin = FPlatformTime::Cycles64();
	}

	FHostLoad FHostLoadMonitor::End(const uint64 Mask, const uint64 CoreMask) const
	{
		const uint64 SiblingMask = CoreMask & ~Mask;
		FHostLoad Out;
		if (!bStarted)
		{
			return Out;
		}
		const uint64 TscEnd = __rdtsc();
		const uint64 QpcEnd = FPlatformTime::Cycles64();
		std::vector<uint64> IdleEnd(IdleCyclesBegin.size(), 0);
		ULONG Bytes = static_cast<ULONG>(IdleEnd.size() * sizeof(ULONG64));
		ULONG64 ProcessCycles = 0;
		if (!QueryIdleProcessorCycleTime(&Bytes, reinterpret_cast<PULONG64>(IdleEnd.data())) || !QueryProcessCycleTime(GetCurrentProcess(), &ProcessCycles))
		{
			return Out;
		}
		const double Elapsed = static_cast<double>(TscEnd - TscBegin);
		if (!(Elapsed > 0.0))
		{
			return Out;
		}
		Out.bValid = true;
		Out.WallSeconds = static_cast<double>(QpcEnd - QpcBegin) * FPlatformTime::GetSecondsPerCycle64();
		const double Own = static_cast<double>(ProcessCycles - ProcessCyclesBegin);
		double MaskBusy = 0.0;
		double SiblingBusy = 0.0;
		double SystemBusy = 0.0;
		Out.PerCpuBusyPct.resize(IdleEnd.size());
		for (size_t Cpu = 0; Cpu < IdleEnd.size(); ++Cpu)
		{
			const double Idle = static_cast<double>(IdleEnd[Cpu] - IdleCyclesBegin[Cpu]);
			const double Busy = std::clamp(Elapsed - Idle, 0.0, Elapsed);
			Out.PerCpuBusyPct[Cpu] = 100.0 * Busy / Elapsed;
			SystemBusy += Busy;
			if ((Mask >> Cpu) & 1ull)
			{
				MaskBusy += Busy;
			}
			else if ((SiblingMask >> Cpu) & 1ull)
			{
				SiblingBusy += Busy;
			}
		}
		Out.OwnProcessCorePct = 100.0 * Own / Elapsed;
		// This process runs on the mask's CPUs only (its affinity), so its cycles come off the mask's busy cycles.
		Out.CoreBusyOtherCorePct = 100.0 * std::max(0.0, MaskBusy + SiblingBusy - Own) / Elapsed;
		Out.MaskBusyOtherCorePct = 100.0 * std::max(0.0, MaskBusy - Own) / Elapsed;
		Out.SiblingBusyCorePct = 100.0 * SiblingBusy / Elapsed;
		Out.SystemBusyOtherCorePct = 100.0 * std::max(0.0, SystemBusy - Own) / Elapsed;
		return Out;
	}

	// ---- FProcessSpawner ------------------------------------------------------------------------------------------------
	FChildResult FProcessSpawner::RunChild(const std::wstring& CommandLine, const double TimeoutSeconds, const std::filesystem::path& LogPath)
	{
		FChildResult Result;
		const uint64 Begin = FPlatformTime::Cycles64();
		SECURITY_ATTRIBUTES Inherit = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		const HANDLE Log = CreateFileW(LogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &Inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		const HANDLE Input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &Inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (Log == INVALID_HANDLE_VALUE || Input == INVALID_HANDLE_VALUE)
		{
			Result.Error = "cannot open the child's log '" + StringConv::PathToUtf8(LogPath) + "' or NUL";
			if (Log != INVALID_HANDLE_VALUE)
			{
				CloseHandle(Log);
			}
			if (Input != INVALID_HANDLE_VALUE)
			{
				CloseHandle(Input);
			}
			return Result;
		}
		// Only the two standard handles are inherited (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), as in the viewer's headless re-run.
		HANDLE Inherited[2] = { Log, Input };
		SIZE_T AttributeBytes = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &AttributeBytes);
		std::vector<uint8> AttributeBuffer(AttributeBytes);
		auto* Attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(AttributeBuffer.data());
		STARTUPINFOEXW Startup = {};
		Startup.StartupInfo.cb = sizeof(Startup);
		Startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
		Startup.StartupInfo.hStdInput = Input;
		Startup.StartupInfo.hStdOutput = Log;
		Startup.StartupInfo.hStdError = Log;
		const bool bAttributes = InitializeProcThreadAttributeList(Attributes, 1, 0, &AttributeBytes)
			&& UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, Inherited, sizeof(Inherited), nullptr, nullptr);
		Startup.lpAttributeList = bAttributes ? Attributes : nullptr;
		// The job kills the child when it is closed, also when this process dies first: no orphan bench process.
		const HANDLE Job = CreateJobObjectW(nullptr, nullptr);
		if (Job != nullptr)
		{
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits = {};
			Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(Job, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits));
		}
		std::wstring Mutable = CommandLine;
		PROCESS_INFORMATION Process = {};
		const DWORD Flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | (bAttributes ? EXTENDED_STARTUPINFO_PRESENT : 0u);
		if (!CreateProcessW(nullptr, Mutable.data(), nullptr, nullptr, TRUE, Flags, nullptr, nullptr, &Startup.StartupInfo, &Process))
		{
			Result.Error = "CreateProcessW failed (error " + std::to_string(GetLastError()) + ")";
		}
		else
		{
			Result.bStarted = true;
			if (Job != nullptr)
			{
				AssignProcessToJobObject(Job, Process.hProcess);
			}
			ResumeThread(Process.hThread);
			const DWORD TimeoutMs = TimeoutSeconds > 0.0 ? static_cast<DWORD>(std::min(TimeoutSeconds * 1000.0, 4.0e9)) : INFINITE;
			if (WaitForSingleObject(Process.hProcess, TimeoutMs) == WAIT_TIMEOUT)
			{
				Result.bTimedOut = true;
				if (Job != nullptr)
				{
					TerminateJobObject(Job, 124);
				}
				else
				{
					TerminateProcess(Process.hProcess, 124);
				}
				WaitForSingleObject(Process.hProcess, 10000);
			}
			DWORD Code = 0;
			GetExitCodeProcess(Process.hProcess, &Code);
			Result.ExitCode = static_cast<int32>(Code);
			CloseHandle(Process.hThread);
			CloseHandle(Process.hProcess);
		}
		if (bAttributes)
		{
			DeleteProcThreadAttributeList(Attributes);
		}
		if (Job != nullptr)
		{
			CloseHandle(Job);
		}
		CloseHandle(Log);
		CloseHandle(Input);
		Result.WallSeconds = static_cast<double>(FPlatformTime::Cycles64() - Begin) * FPlatformTime::GetSecondsPerCycle64();
		return Result;
	}

	// ---- clock facts ----------------------------------------------------------------------------------------------------
	double MeasureClockReadOverheadNs(const uint32 Reads)
	{
		const uint32 N = Reads > 0 ? Reads : 1;
		std::vector<double> Batches;
		Batches.reserve(32);
		volatile uint64 Sink = 0;
		for (int Batch = 0; Batch < 32; ++Batch)
		{
			const uint64 Begin = FClockBuckets::Begin();
			for (uint32 Index = 0; Index < N; ++Index)
			{
				Sink = Sink + FClockBuckets::Begin();
			}
			const uint64 Elapsed = FClockBuckets::End(Begin);
			Batches.push_back(FClockBuckets::TicksToNs(Elapsed) / static_cast<double>(N));
		}
		std::sort(Batches.begin(), Batches.end());
		return 0.5 * (Batches[15] + Batches[16]);
	}
}
