// PhysXParse.h - option text parsing of the PhysX backend (scene and init options of a backend variant). Header-only,
// backend namespace only. [MVP M5]   PT_BACKEND_NS=PhysX
#pragma once

#include "PhysicsTestAbi/PtBackendKit.h"
#include "PtTypes.h"

#include <stdlib.h>
#include <string.h>

namespace PhysicsTest::PhysXBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	inline char ToLowerAscii(const char C)
	{
		return (C >= 'A' && C <= 'Z') ? static_cast<char>(C - 'A' + 'a') : C;
	}

	inline bool EqualsNoCase(const char* A, const ::PhysicsTest::Abi::FStringRef B)
	{
		const size_t Length = strlen(A);
		if (Length != B.Length)
		{
			return false;
		}
		for (size_t Index = 0; Index < Length; ++Index)
		{
			if (ToLowerAscii(A[Index]) != ToLowerAscii(B.Data[Index]))
			{
				return false;
			}
		}
		return true;
	}

	inline ::PhysicsTest::Abi::FStringRef TrimSpaces(::PhysicsTest::Abi::FStringRef Text)
	{
		while (Text.Length > 0 && (Text.Data[0] == ' ' || Text.Data[0] == '\t'))
		{
			++Text.Data;
			--Text.Length;
		}
		while (Text.Length > 0 && (Text.Data[Text.Length - 1] == ' ' || Text.Data[Text.Length - 1] == '\t'))
		{
			--Text.Length;
		}
		return Text;
	}

	inline bool ParseDouble(const ::PhysicsTest::Abi::FStringRef Text, double& Out)
	{
		char Buffer[64];
		if (Text.Length == 0 || Text.Length >= sizeof(Buffer))
		{
			return false;
		}
		memcpy(Buffer, Text.Data, Text.Length);
		Buffer[Text.Length] = '\0';
		char* End = nullptr;
		const double Value = strtod(Buffer, &End);
		if (End == Buffer || *End != '\0' || !::PhysicsTest::Abi::IsFinite(Value))
		{
			return false;
		}
		Out = Value;
		return true;
	}

	inline bool ParseUInt(const ::PhysicsTest::Abi::FStringRef Text, uint64& Out)
	{
		char Buffer[32];
		if (Text.Length == 0 || Text.Length >= sizeof(Buffer) || Text.Data[0] == '-' || Text.Data[0] == '+')
		{
			return false;
		}
		memcpy(Buffer, Text.Data, Text.Length);
		Buffer[Text.Length] = '\0';
		char* End = nullptr;
		const unsigned long long Value = strtoull(Buffer, &End, 10);
		if (End == Buffer || *End != '\0')
		{
			return false;
		}
		Out = Value;
		return true;
	}

	inline bool ParseBool(const ::PhysicsTest::Abi::FStringRef Text, bool& Out)
	{
		if (EqualsNoCase("true", Text) || EqualsNoCase("1", Text))
		{
			Out = true;
			return true;
		}
		if (EqualsNoCase("false", Text) || EqualsNoCase("0", Text))
		{
			Out = false;
			return true;
		}
		return false;
	}
}
