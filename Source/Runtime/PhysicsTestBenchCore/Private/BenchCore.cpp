// BenchCore.cpp - see BenchCore.h.
#include "BenchCore.h"

#include "Containers/StringConv.h"
#include "HAL/PlatformTime.h"
#include "Hash/Sha256.h"

#include <Windows.h>
#include <combaseapi.h>   // StringFromGUID2 (WIN32_LEAN_AND_MEAN leaves out ole2.h)
#include <intrin.h>
#include <powrprof.h>
#include <psapi.h>        // GetProcessMemoryInfo (K32GetProcessMemoryInfo in KERNEL32, PSAPI_VERSION 2)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>

namespace PhysicsTest
{
	uint64 FClockBuckets::Begin()
	{
		return FPlatformTime::Cycles64();
	}

	double FClockBuckets::TicksToNs(const uint64 Ticks)
	{
		return static_cast<double>(Ticks) * 1.0e9 * FPlatformTime::GetSecondsPerCycle64();
	}

	uint64 FClockBuckets::GetTicksPerSecond()
	{
		return FPlatformTime::GetCyclesPerSecond64();
	}

	FDurationStats ComputeDurationStats(const std::vector<double>& Values)
	{
		FDurationStats Stats;
		if (Values.empty())
		{
			return Stats;
		}
		std::vector<double> Sorted = Values;
		std::sort(Sorted.begin(), Sorted.end());
		const size_t N = Sorted.size();
		double Sum = 0.0;
		for (const double Value : Sorted)
		{
			Sum += Value;
		}
		const auto Rank = [&Sorted, N](const double P)
		{
			// Nearest rank: the ceil(P * N)-th smallest value (1-based).
			size_t Index = static_cast<size_t>(std::ceil(P * static_cast<double>(N)));
			Index = Index < 1 ? 1 : (Index > N ? N : Index);
			return Sorted[Index - 1];
		};
		Stats.Count = static_cast<uint32>(N);
		Stats.Mean = Sum / static_cast<double>(N);
		Stats.Median = (N % 2 == 1) ? Sorted[N / 2] : 0.5 * (Sorted[N / 2 - 1] + Sorted[N / 2]);
		Stats.P95 = Rank(0.95);
		Stats.P99 = Rank(0.99);
		Stats.Min = Sorted.front();
		Stats.Max = Sorted.back();
		return Stats;
	}

	std::string MakeRunId()
	{
		static const char Alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";   // Crockford base32
		const uint64 Milliseconds = static_cast<uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
		std::random_device Device;
		uint8 Bytes[16] = {};
		for (int Index = 0; Index < 6; ++Index)
		{
			Bytes[Index] = static_cast<uint8>(Milliseconds >> (8 * (5 - Index)));
		}
		for (int Index = 6; Index < 16; ++Index)
		{
			Bytes[Index] = static_cast<uint8>(Device() & 0xFFu);
		}
		// 128 bits -> 26 base32 digits (the first digit carries the top 3 bits).
		std::string Id(26, '0');
		for (int Digit = 25; Digit >= 0; --Digit)
		{
			const int BitOffset = (25 - Digit) * 5;   // bits from the least significant end
			uint32 Value = 0;
			for (int Bit = 0; Bit < 5; ++Bit)
			{
				const int Position = BitOffset + Bit;   // 0 = LSB of the 128-bit number
				if (Position < 128)
				{
					const int ByteIndex = 15 - Position / 8;
					Value |= ((Bytes[ByteIndex] >> (Position % 8)) & 1u) << Bit;
				}
			}
			Id[static_cast<size_t>(Digit)] = Alphabet[Value];
		}
		return Id;
	}

	std::string GetUtcTimestamp()
	{
		const std::time_t Now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm Utc = {};
		gmtime_s(&Utc, &Now);
		char Text[32];
		std::strftime(Text, sizeof(Text), "%Y-%m-%dT%H:%M:%SZ", &Utc);
		return Text;
	}

	std::string HashFileSha256(const std::filesystem::path& Path)
	{
		std::ifstream File(Path, std::ios::binary);
		if (!File)
		{
			return std::string();
		}
		FSHA256 Sha;
		std::vector<char> Buffer(1 << 20);
		while (File)
		{
			File.read(Buffer.data(), static_cast<std::streamsize>(Buffer.size()));
			const std::streamsize Read = File.gcount();
			if (Read > 0)
			{
				Sha.Update(Buffer.data(), static_cast<uint64>(Read));
			}
		}
		return Sha.Final().ToString();
	}

	FHostFacts ReadHostFacts()
	{
		FHostFacts Facts;
		int Registers[4] = {};
		char Brand[49] = {};
		__cpuid(Registers, static_cast<int>(0x80000000u));
		if (static_cast<unsigned>(Registers[0]) >= 0x80000004u)
		{
			for (int Leaf = 0; Leaf < 3; ++Leaf)
			{
				__cpuid(Registers, static_cast<int>(0x80000002u + static_cast<unsigned>(Leaf)));
				std::memcpy(Brand + Leaf * 16, Registers, sizeof(Registers));
			}
		}
		Facts.CpuBrand = Brand;
		while (!Facts.CpuBrand.empty() && Facts.CpuBrand.back() == ' ')
		{
			Facts.CpuBrand.pop_back();
		}
		Facts.LogicalProcessors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);

		// RtlGetVersion reports the real build (GetVersionEx is manifest-dependent).
		using FRtlGetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
		if (const HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll"))
		{
			if (const auto RtlGetVersion = reinterpret_cast<FRtlGetVersion>(reinterpret_cast<void*>(GetProcAddress(Ntdll, "RtlGetVersion"))))
			{
				RTL_OSVERSIONINFOW Version = {};
				Version.dwOSVersionInfoSize = sizeof(Version);
				if (RtlGetVersion(&Version) == 0)
				{
					Facts.OsBuild = std::to_string(Version.dwMajorVersion) + "." + std::to_string(Version.dwMinorVersion) + "." + std::to_string(Version.dwBuildNumber);
				}
			}
		}

		// The active power scheme: read only (the user switches it, 03 2 / 01 Q9).
		GUID* Scheme = nullptr;
		if (PowerGetActiveScheme(nullptr, &Scheme) == ERROR_SUCCESS && Scheme != nullptr)
		{
			wchar_t GuidText[64] = {};
			StringFromGUID2(*Scheme, GuidText, 64);
			Facts.PowerSchemeGuid = StringConv::WideToUtf8(GuidText);
			DWORD Size = 0;
			if (PowerReadFriendlyName(nullptr, Scheme, nullptr, nullptr, nullptr, &Size) == ERROR_SUCCESS && Size > 0)
			{
				std::vector<UCHAR> Name(Size);
				if (PowerReadFriendlyName(nullptr, Scheme, nullptr, nullptr, Name.data(), &Size) == ERROR_SUCCESS)
				{
					Facts.PowerSchemeName = StringConv::WideToUtf8(reinterpret_cast<const wchar_t*>(Name.data()));
				}
			}
			LocalFree(Scheme);
		}
		Facts.QpcHz = FPlatformTime::GetCyclesPerSecond64();
		DWORD_PTR ProcessMask = 0;
		DWORD_PTR SystemMask = 0;
		if (GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask))
		{
			Facts.ProcessAffinityMask = static_cast<uint64>(ProcessMask);
		}
		switch (GetPriorityClass(GetCurrentProcess()))
		{
		case IDLE_PRIORITY_CLASS: Facts.PriorityClass = "IDLE"; break;
		case BELOW_NORMAL_PRIORITY_CLASS: Facts.PriorityClass = "BELOW_NORMAL"; break;
		case NORMAL_PRIORITY_CLASS: Facts.PriorityClass = "NORMAL"; break;
		case ABOVE_NORMAL_PRIORITY_CLASS: Facts.PriorityClass = "ABOVE_NORMAL"; break;
		case HIGH_PRIORITY_CLASS: Facts.PriorityClass = "HIGH"; break;
		case REALTIME_PRIORITY_CLASS: Facts.PriorityClass = "REALTIME"; break;
		default: Facts.PriorityClass = "?"; break;
		}
		return Facts;
	}

	FProcessMemory ReadProcessMemoryCounters()
	{
		FProcessMemory Memory;
		PROCESS_MEMORY_COUNTERS_EX Counters = {};
		Counters.cb = sizeof(Counters);
		if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&Counters), sizeof(Counters)))
		{
			Memory.bValid = true;
			Memory.CommitBytes = Counters.PrivateUsage;
			Memory.PeakCommitBytes = Counters.PeakPagefileUsage;
			Memory.WorkingSetBytes = Counters.WorkingSetSize;
			Memory.PeakWorkingSetBytes = Counters.PeakWorkingSetSize;
		}
		return Memory;
	}
}
