// LogMacros.cpp - UE_LOG backend: one formatted line per call, serialized across threads.
#include "Logging/LogMacros.h"

#include "HAL/PlatformMisc.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

DEFINE_LOG_CATEGORY(LogTemp);
DEFINE_LOG_CATEGORY(LogCore);
DEFINE_LOG_CATEGORY(LogPhysicsTest);

namespace PhysicsTest
{
	const char* ToString(const ELogVerbosity Verbosity)
	{
		switch (Verbosity)
		{
		case ELogVerbosity::NoLogging:   return "NoLogging";
		case ELogVerbosity::Fatal:       return "Fatal";
		case ELogVerbosity::Error:       return "Error";
		case ELogVerbosity::Warning:     return "Warning";
		case ELogVerbosity::Display:     return "Display";
		case ELogVerbosity::Log:         return "Log";
		case ELogVerbosity::Verbose:     return "Verbose";
		case ELogVerbosity::VeryVerbose: return "VeryVerbose";
		}
		return "Unknown";
	}

	namespace Logging
	{
		namespace
		{
			std::mutex& GetLogMutex()
			{
				static std::mutex Mutex;
				return Mutex;
			}

			std::atomic<bool> GAllLinesToStderr{ false };
		}

		void SetAllLinesToStderr(const bool bAllToStderr)
		{
			GAllLinesToStderr.store(bAllToStderr, std::memory_order_relaxed);
		}

		void Write(const FLogCategoryBase& Category, const ELogVerbosity Verbosity, const char* File, const int32 Line, const char* Format, ...)
		{
			char Message[4096];
			va_list Args;
			va_start(Args, Format);
			std::vsnprintf(Message, sizeof(Message), Format, Args);
			va_end(Args);

			char LineBuffer[4608];
			if (Verbosity == ELogVerbosity::Log)
			{
				std::snprintf(LineBuffer, sizeof(LineBuffer), "%s: %s\n", Category.GetCategoryName(), Message);
			}
			else if (Verbosity == ELogVerbosity::Fatal)
			{
				std::snprintf(LineBuffer, sizeof(LineBuffer), "%s: Fatal: %s [%s(%d)]\n", Category.GetCategoryName(), Message, File, Line);
			}
			else
			{
				std::snprintf(LineBuffer, sizeof(LineBuffer), "%s: %s: %s\n", Category.GetCategoryName(), ToString(Verbosity), Message);
			}

			{
				std::lock_guard<std::mutex> Lock(GetLogMutex());
				FILE* Stream = (Verbosity <= ELogVerbosity::Warning || GAllLinesToStderr.load(std::memory_order_relaxed)) ? stderr : stdout;
				std::fputs(LineBuffer, Stream);
				std::fflush(Stream);
				if (FPlatformMisc::IsDebuggerPresent())
				{
					FPlatformMisc::LocalPrint(LineBuffer);
				}
			}

			if (Verbosity == ELogVerbosity::Fatal)
			{
				FPlatformMisc::DebugBreakIfDebuggerPresent();
				FPlatformMisc::RequestExitFatal(3);
			}
		}
	}
}
