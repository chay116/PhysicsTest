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
		// Terminates the process immediately with ExitCode after flushing stdio (used by check() and Fatal logs). The
		// callback set with SetFatalExitCallback runs first, once (M7: the bench writes exit.json with the Fatal code, so its
		// runner never has to trust a raw process exit code, 02_Architecture 12.2).
		[[noreturn]] static void RequestExitFatal(int32 ExitCode);
		// Harness-specific (M7). Message: the Fatal log line that requested the exit ("" when none). The callback must not
		// throw and must not log a Fatal itself; a second Fatal inside it exits without calling it again. nullptr removes it.
		using FFatalExitCallback = void (*)(int32 ExitCode, const char* Message);
		static void SetFatalExitCallback(FFatalExitCallback Callback);
		// The text RequestExitFatal hands to the callback (Logging::Write sets it for a Fatal line; truncated to 1 KiB).
		static void SetFatalMessage(const char* Message);
	};
}
