// Parse.h - command-line style parsing (UE: Core/Public/Misc/Parse.h), minimal (DL18).
// Matching follows UE (Core/Private/Misc/Parse.cpp, Core/Public/Misc/CString.h):
//   A key is found by a case-insensitive search that skips text inside double quotes and only matches where the
//   preceding character is not alphanumeric [A-Za-z0-9] (UE FCString::Strifind, CString.h:722-760). So "Steps=" does
//   not match inside "-WarmupSteps=10", and "Threads=" does not match inside "-PhysXThreads=4"; '_' and '-' are not
//   alphanumeric, so "Steps=" does match inside "-Warmup_Steps=5" (as in UE).
//   Param(Stream, "Foo")       true when "-Foo" or "/Foo" is a whole whitespace-separated token outside quotes
//                              (case-insensitive) (UE behaviour: Parse.cpp:325-350)
//   Value(Stream, "Key=", Out) the text after the first match: a value in double quotes may contain spaces; an
//                              unquoted value skips leading whitespace and stops at whitespace, ',' or ')'
//                              (UE behaviour: Parse.cpp:242-322)
// Intentional differences from UE (docs/Progress.md M0 §3 #13, M1 §3, M3 §3): the numeric overloads return true only
// when a number was actually read, is the whole value (followed only by the end, whitespace, ',', ')' or a closing quote:
// "-Threads=4x" and "-Threads=1.5" are errors) and fits the type (UE returns true for any match and converts with
// Atoi/Atof, so "-Threads=abc" gives 0); the unsigned overloads also reject a '-' sign ("-Threads=-1" is an error, not
// 4294967295) and read a 0x / 0X prefix as hexadecimal ("-Affinity=0x55" is 85); the float / double overloads reject inf
// and nan; and Bool returns false for a value it does not recognise (UE returns true with FCString::ToBool's result,
// false).
#pragma once

#include "CoreTypes.h"

#include <string>

namespace PhysicsTest
{
	struct FParse
	{
		[[nodiscard]] static bool Param(const char* Stream, const char* Param);

		static bool Value(const char* Stream, const char* Match, std::string& Value, bool bShouldStopOnSeparator = true);
		static bool Value(const char* Stream, const char* Match, int32& Value);
		static bool Value(const char* Stream, const char* Match, uint32& Value);
		static bool Value(const char* Stream, const char* Match, int64& Value);
		static bool Value(const char* Stream, const char* Match, uint64& Value);
		static bool Value(const char* Stream, const char* Match, float& Value);
		static bool Value(const char* Stream, const char* Match, double& Value);

		// "Key=true|false|on|off|yes|no|<integer>" (case-insensitive; an integer is true when non-zero), like
		// UE's FParse::Bool / FCString::ToBool.
		static bool Bool(const char* Stream, const char* Match, bool& OnOff);

		// Next token of Stream, which is advanced past it (UE FParse::Token without escapes, Parse.cpp:846-930): leading
		// whitespace (and Delimiter, when given) is skipped; a token that starts with '"' runs to the closing quote and
		// loses the quotes; any other token runs to whitespace / Delimiter and keeps embedded quoted parts intact
		// ("-Key="a b"" is one token). Returns false when no token is left.
		static bool Token(const char*& Stream, std::string& OutToken, char Delimiter = '\0');
	};
}
