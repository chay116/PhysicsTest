// CommandLine.cpp - process-wide command line string (UTF-8) and the wide argv conversion for wmain.
#include "Misc/CommandLine.h"

#include "Containers/StringConv.h"

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

	FUtf8ArgV::FUtf8ArgV(const int32 ArgC, wchar_t** ArgVW)
	{
		const int32 Count = (ArgVW != nullptr && ArgC > 0) ? ArgC : 0;
		Strings.reserve(static_cast<size_t>(Count));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Strings.push_back(ArgVW[Index] != nullptr ? StringConv::WideToUtf8(ArgVW[Index]) : std::string());
		}
		// Pointers into Strings are taken only after Strings stopped growing.
		Pointers.reserve(Strings.size() + 1);
		for (std::string& String : Strings)
		{
			Pointers.push_back(String.data());
		}
		Pointers.push_back(nullptr);
	}
}
