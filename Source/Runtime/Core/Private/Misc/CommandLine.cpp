// CommandLine.cpp - process-wide command line string.
#include "Misc/CommandLine.h"

#include <cstring>

namespace PhysicsTest
{
	namespace
	{
		std::string& CommandLineStorage()
		{
			static std::string Storage;
			return Storage;
		}
	}

	void FCommandLine::Set(const char* NewCommandLine)
	{
		CommandLineStorage() = NewCommandLine != nullptr ? NewCommandLine : "";
	}

	void FCommandLine::Append(const char* AppendString)
	{
		if (AppendString != nullptr)
		{
			CommandLineStorage() += AppendString;
		}
	}

	const char* FCommandLine::Get()
	{
		return CommandLineStorage().c_str();
	}

	std::string FCommandLine::BuildFromArgV(const char* Prefix, const int32 ArgC, char** ArgV, const char* Suffix)
	{
		std::string Result = Prefix != nullptr ? Prefix : "";
		for (int32 Index = 1; Index < ArgC; ++Index)
		{
			if (!Result.empty())
			{
				Result += ' ';
			}
			const char* Arg = ArgV[Index];
			const bool bHasSpace = std::strchr(Arg, ' ') != nullptr;
			const char* Equals = std::strchr(Arg, '=');
			if (bHasSpace && Equals != nullptr)
			{
				// -Key=Value with spaces -> -Key="Value with spaces" (UE keeps the key unquoted).
				Result.append(Arg, Equals + 1);
				Result += '"';
				Result += Equals + 1;
				Result += '"';
			}
			else if (bHasSpace)
			{
				Result += '"';
				Result += Arg;
				Result += '"';
			}
			else
			{
				Result += Arg;
			}
		}
		if (Suffix != nullptr && *Suffix != '\0')
		{
			if (!Result.empty())
			{
				Result += ' ';
			}
			Result += Suffix;
		}
		return Result;
	}
}
