// WindowsPlatform.cpp - FPlatformTime (QPC) and FPlatformMisc on Win32.
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

#include <Windows.h>

#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>

namespace PhysicsTest
{
	namespace
	{
		// Function-local statics: safe to use from other static initialisers.
		uint64 QpcFrequency()
		{
			static const uint64 Frequency = []
			{
				LARGE_INTEGER Value;
				QueryPerformanceFrequency(&Value);   // fixed at boot, never fails on Windows XP and later
				return static_cast<uint64>(Value.QuadPart);
			}();
			return Frequency;
		}

		double SecondsPerCycle()
		{
			static const double Value = 1.0 / static_cast<double>(QpcFrequency());
			return Value;
		}
	}

	uint64 FPlatformTime::Cycles64()
	{
		LARGE_INTEGER Counter;
		QueryPerformanceCounter(&Counter);
		return static_cast<uint64>(Counter.QuadPart);
	}

	uint64 FPlatformTime::GetCyclesPerSecond64()
	{
		return QpcFrequency();
	}

	double FPlatformTime::GetSecondsPerCycle64()
	{
		return SecondsPerCycle();
	}

	double FPlatformTime::Seconds()
	{
		return static_cast<double>(Cycles64()) * SecondsPerCycle();
	}

	bool FPlatformMisc::IsDebuggerPresent()
	{
		return ::IsDebuggerPresent() != FALSE;
	}

	void FPlatformMisc::DebugBreakIfDebuggerPresent()
	{
		if (IsDebuggerPresent())
		{
			__debugbreak();
		}
	}

	void FPlatformMisc::LocalPrint(const char* Text)
	{
		OutputDebugStringA(Text);
	}

	void FPlatformMisc::SuppressErrorDialogs()
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	}

	void FPlatformMisc::RequestExitFatal(const int32 ExitCode)
	{
		std::fflush(stdout);
		std::fflush(stderr);
		std::_Exit(ExitCode);
	}
}
