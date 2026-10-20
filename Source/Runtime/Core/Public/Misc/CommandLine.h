// CommandLine.h - process command line as one string (UE: Core/Public/Misc/CommandLine.h), minimal (DL18).
// Parse it with FParse::Param / FParse::Value, e.g. FParse::Value(FCommandLine::Get(), "Threads=", NumThreads).
#pragma once

#include "CoreTypes.h"

#include <string>

namespace PhysicsTest
{
	struct FCommandLine
	{
		static void Set(const char* NewCommandLine);
		static void Append(const char* AppendString);
		[[nodiscard]] static const char* Get();
		// Joins argv[1..] with spaces, quoting arguments that contain spaces (UE: FCommandLine::BuildFromArgV).
		[[nodiscard]] static std::string BuildFromArgV(const char* Prefix, int32 ArgC, char** ArgV, const char* Suffix);
	};
}
