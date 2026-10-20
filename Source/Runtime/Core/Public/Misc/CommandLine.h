// CommandLine.h - process command line as one string (UE: Core/Public/Misc/CommandLine.h), minimal (DL18).
// Parse it with FParse::Param / FParse::Value, e.g. FParse::Value(FCommandLine::Get(), "Threads=", NumThreads).
// The string is UTF-8. Harness programs enter through wmain and convert the UTF-16 argv with FUtf8ArgV, so non-ASCII
// arguments (Korean paths, labels) survive; main(int, char**) would receive them in the ANSI code page (UE builds its
// command line from TCHAR as well).
#pragma once

#include "CoreTypes.h"

#include <string>
#include <vector>

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

	// UTF-8 copy of a wide argv (wmain), with a stable char** view for FCommandLine::BuildFromArgV and doctest.
	//
	//   int wmain(int ArgC, wchar_t** ArgVW)
	//   {
	//       const FUtf8ArgV Args(ArgC, ArgVW);
	//       FCommandLine::Set(FCommandLine::BuildFromArgV(nullptr, Args.Num(), Args.Get(), nullptr).c_str());
	class FUtf8ArgV
	{
	public:
		FUtf8ArgV(int32 ArgC, wchar_t** ArgVW);
		FUtf8ArgV(const FUtf8ArgV&) = delete;
		FUtf8ArgV& operator=(const FUtf8ArgV&) = delete;

		[[nodiscard]] int32 Num() const { return static_cast<int32>(Strings.size()); }
		// argv-style array: Num() entries followed by nullptr.
		[[nodiscard]] char** Get() const { return const_cast<char**>(Pointers.data()); }
		[[nodiscard]] const std::string& operator[](const int32 Index) const { return Strings[static_cast<size_t>(Index)]; }

	private:
		std::vector<std::string> Strings;
		std::vector<char*> Pointers;
	};
}
