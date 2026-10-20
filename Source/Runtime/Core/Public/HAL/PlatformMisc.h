// PlatformMisc.h - the few platform helpers the harness needs (UE: Windows/WindowsPlatformMisc.h, subset).
#pragma once

#include "CoreTypes.h"

namespace PhysicsTest
{
	struct FPlatformMisc
	{
		[[nodiscard]] static bool IsDebuggerPresent();
		// Breaks into an attached debugger; no-op otherwise.
		static void DebugBreakIfDebuggerPresent();
		static void LocalPrint(const char* Text);
		// For unattended runs (CTest, bench): no WER / abort / CRT-assert message boxes, reports go to stderr.
		// Harness-specific helper; UE achieves the same through -unattended.
		static void SuppressErrorDialogs();
		// Terminates the process immediately with ExitCode after flushing stdio (used by check() and Fatal logs).
		[[noreturn]] static void RequestExitFatal(int32 ExitCode);
	};
}
