// AssertionMacros.cpp - failure handlers behind check / verify / ensure.
#include "Misc/AssertionMacros.h"

#include "HAL/PlatformMisc.h"
#include "Logging/LogMacros.h"

#include <cstdarg>
#include <cstdio>

namespace PhysicsTest::Assert
{
	void OnCheckFailed(const char* Expr, const char* File, const int32 Line)
	{
		::PhysicsTest::Logging::Write(LogCore, ELogVerbosity::Fatal, File, Line, "Assertion failed: %s", Expr);
		FPlatformMisc::RequestExitFatal(3);   // not reached: Fatal terminates
	}

	void OnCheckFailedf(const char* Expr, const char* File, const int32 Line, const char* Format, ...)
	{
		char Message[2048];
		va_list Args;
		va_start(Args, Format);
		std::vsnprintf(Message, sizeof(Message), Format, Args);
		va_end(Args);
		::PhysicsTest::Logging::Write(LogCore, ELogVerbosity::Fatal, File, Line, "Assertion failed: %s %s", Expr, Message);
		FPlatformMisc::RequestExitFatal(3);
	}

	bool OnEnsureFailed(bool& bExecuted, const char* Expr, const char* File, const int32 Line)
	{
		if (!bExecuted)
		{
			bExecuted = true;
			::PhysicsTest::Logging::Write(LogCore, ELogVerbosity::Error, File, Line, "Ensure condition failed: %s [%s(%d)]", Expr, File, Line);
			FPlatformMisc::DebugBreakIfDebuggerPresent();
		}
		return false;
	}

	bool OnEnsureFailedf(bool& bExecuted, const char* Expr, const char* File, const int32 Line, const char* Format, ...)
	{
		if (!bExecuted)
		{
			bExecuted = true;
			char Message[2048];
			va_list Args;
			va_start(Args, Format);
			std::vsnprintf(Message, sizeof(Message), Format, Args);
			va_end(Args);
			::PhysicsTest::Logging::Write(LogCore, ELogVerbosity::Error, File, Line, "Ensure condition failed: %s [%s(%d)] %s", Expr, File, Line, Message);
			FPlatformMisc::DebugBreakIfDebuggerPresent();
		}
		return false;
	}
}
