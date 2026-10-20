// Parse.cpp - FParse::Param / Value / Bool. Behaviour follows UE (Core/Private/Misc/Parse.cpp:242-350,
// FCString::Strifind in Core/Public/Misc/CString.h:722-760); the code and its structure are our own.
// The intentional differences (numeric overloads, unrecognised Bool values) are listed in Misc/Parse.h.
#include "Misc/Parse.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace PhysicsTest
{
	namespace
	{
		// ASCII whitespace as UE's FChar::IsWhitespace sees it (iswspace) for command-line text.
		bool IsWhitespace(const char C)
		{
			return C == ' ' || C == '\t' || C == '\r' || C == '\n' || C == '\v' || C == '\f';
		}

		// ASCII only, like UE's Strifind (which upper-cases a-z and tests A-Z / 0-9).
		bool IsAsciiAlnum(const char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9');
		}

		char ToAsciiLower(const char C)
		{
			return (C >= 'A' && C <= 'Z') ? static_cast<char>(C - 'A' + 'a') : C;
		}

		bool EqualsAsciiNoCase(const char* A, const char* B, const size_t Length)
		{
			for (size_t Index = 0; Index < Length; ++Index)
			{
				if (A[Index] == '\0' || ToAsciiLower(A[Index]) != ToAsciiLower(B[Index]))
				{
					return false;
				}
			}
			return true;
		}

		// Finds Key in Stream: case-insensitive, never inside double quotes, and only where the preceding character is
		// not [A-Za-z0-9] (or at the start of the stream). This is the matching rule of UE's
		// FCString::Strifind(Str, Find, bSkipQuotedChars = true) (CString.h:722-760), which FParse::Value and
		// FParse::Param are built on. A quote character toggles the quoted state after it has been tested.
		const char* FindKey(const char* Stream, const char* Key)
		{
			if (Stream == nullptr || Key == nullptr || *Key == '\0')
			{
				return nullptr;   // UE: an empty key never matches
			}
			const size_t KeyLength = std::strlen(Key);
			bool bInQuotes = false;
			bool bAfterAlnum = false;
			for (const char* Cursor = Stream; *Cursor != '\0'; ++Cursor)
			{
				if (!bInQuotes && !bAfterAlnum && EqualsAsciiNoCase(Cursor, Key, KeyLength))
				{
					return Cursor;
				}
				bAfterAlnum = IsAsciiAlnum(*Cursor);
				if (*Cursor == '"')
				{
					bInQuotes = !bInQuotes;
				}
			}
			return nullptr;
		}

		const char* FindValueStart(const char* Stream, const char* Match)
		{
			const char* Found = FindKey(Stream, Match);
			return Found != nullptr ? Found + std::strlen(Match) : nullptr;
		}

		template <typename T, typename FParser>
		bool ParseNumber(const char* Stream, const char* Match, T& OutValue, FParser Parser)
		{
			const char* Start = FindValueStart(Stream, Match);
			if (Start == nullptr)
			{
				return false;
			}
			char* End = nullptr;
			errno = 0;
			const auto Parsed = Parser(Start, &End);
			if (End == Start || errno == ERANGE)
			{
				return false;
			}
			OutValue = static_cast<T>(Parsed);
			return true;
		}

		// Integers are read as 64-bit and range-checked, so an out-of-range value is rejected instead of being
		// truncated (long is 32-bit on Windows, and strtoul / strtoull accept a '-' sign and wrap it around:
		// "-1" would become 4294967295 without ERANGE).
		template <typename T>
		bool ParseSigned(const char* Stream, const char* Match, T& OutValue)
		{
			long long Parsed = 0;
			if (!ParseNumber(Stream, Match, Parsed, [](const char* S, char** E) { return std::strtoll(S, E, 10); }))
			{
				return false;
			}
			if (Parsed < static_cast<long long>(std::numeric_limits<T>::min()) || Parsed > static_cast<long long>(std::numeric_limits<T>::max()))
			{
				return false;
			}
			OutValue = static_cast<T>(Parsed);
			return true;
		}

		template <typename T>
		bool ParseUnsigned(const char* Stream, const char* Match, T& OutValue)
		{
			const char* Start = FindValueStart(Stream, Match);
			if (Start == nullptr)
			{
				return false;
			}
			const char* Digits = Start + std::strspn(Start, " \t\r\n\v\f");   // strtoull skips the same whitespace
			if (*Digits == '-')
			{
				return false;   // harness: a negative value for an unsigned option is an error, never a wrap-around
			}
			unsigned long long Parsed = 0;
			if (!ParseNumber(Stream, Match, Parsed, [](const char* S, char** E) { return std::strtoull(S, E, 10); }))
			{
				return false;
			}
			if (Parsed > static_cast<unsigned long long>(std::numeric_limits<T>::max()))
			{
				return false;
			}
			OutValue = static_cast<T>(Parsed);
			return true;
		}
	}

	bool FParse::Param(const char* Stream, const char* Param)
	{
		if (Stream == nullptr || Param == nullptr)
		{
			return false;
		}
		if (*Param == '-' || *Param == '/')
		{
			++Param;
		}
		const size_t ParamLength = std::strlen(Param);
		if (ParamLength == 0)
		{
			return false;
		}

		// Walk the whitespace-separated tokens and track double quotes across them. A token that starts inside a
		// quoted section is never a switch, otherwise "-Param" / "/Param" must be the whole token.
		bool bInQuotes = false;
		const char* Cursor = Stream;
		while (*Cursor != '\0')
		{
			if (IsWhitespace(*Cursor))
			{
				++Cursor;
				continue;
			}
			const char* const TokenBegin = Cursor;
			const bool bTokenQuoted = bInQuotes;
			while (*Cursor != '\0' && !IsWhitespace(*Cursor))
			{
				if (*Cursor == '"')
				{
					bInQuotes = !bInQuotes;
				}
				++Cursor;
			}
			const size_t TokenLength = static_cast<size_t>(Cursor - TokenBegin);
			const bool bSwitchPrefix = *TokenBegin == '-' || *TokenBegin == '/';
			if (!bTokenQuoted && bSwitchPrefix && TokenLength == ParamLength + 1
				&& EqualsAsciiNoCase(TokenBegin + 1, Param, ParamLength))
			{
				return true;
			}
		}
		return false;
	}

	bool FParse::Value(const char* Stream, const char* Match, std::string& Value, const bool bShouldStopOnSeparator)
	{
		const char* Start = FindValueStart(Stream, Match);
		if (Start == nullptr)
		{
			return false;
		}
		const char* End = nullptr;
		if (*Start == '"')
		{
			// -Option="Value1 Value2"
			++Start;
			End = std::strchr(Start, '"');
			if (End == nullptr)
			{
				End = Start + std::strlen(Start);
			}
		}
		else
		{
			Start += std::strspn(Start, " \r\n\t");
			const char* Terminators = bShouldStopOnSeparator ? ",) \r\n\t" : " \r\n\t";
			End = Start + std::strcspn(Start, Terminators);
		}
		Value.assign(Start, End);
		return true;
	}

	bool FParse::Value(const char* Stream, const char* Match, int32& Value)
	{
		return ParseSigned(Stream, Match, Value);
	}

	bool FParse::Value(const char* Stream, const char* Match, uint32& Value)
	{
		return ParseUnsigned(Stream, Match, Value);
	}

	bool FParse::Value(const char* Stream, const char* Match, int64& Value)
	{
		return ParseSigned(Stream, Match, Value);
	}

	bool FParse::Value(const char* Stream, const char* Match, uint64& Value)
	{
		return ParseUnsigned(Stream, Match, Value);
	}

	// strtof / strtod also read "inf", "infinity" and "nan": a real number is required (Parse.h).
	bool FParse::Value(const char* Stream, const char* Match, float& Value)
	{
		float Parsed = 0.0f;
		if (!ParseNumber(Stream, Match, Parsed, [](const char* S, char** E) { return std::strtof(S, E); }) || !std::isfinite(Parsed))
		{
			return false;
		}
		Value = Parsed;
		return true;
	}

	bool FParse::Value(const char* Stream, const char* Match, double& Value)
	{
		double Parsed = 0.0;
		if (!ParseNumber(Stream, Match, Parsed, [](const char* S, char** E) { return std::strtod(S, E); }) || !std::isfinite(Parsed))
		{
			return false;
		}
		Value = Parsed;
		return true;
	}

	bool FParse::Bool(const char* Stream, const char* Match, bool& OnOff)
	{
		std::string Text;
		if (!Value(Stream, Match, Text))
		{
			return false;
		}
		const auto Is = [&Text](const char* Word)
		{
			return Text.size() == std::strlen(Word) && EqualsAsciiNoCase(Text.c_str(), Word, Text.size());
		};
		// UE FCString::ToBool (CString.cpp:123-153): True/Yes/On, False/No/Off, otherwise Atoi(Text) != 0.
		if (Is("true") || Is("yes") || Is("on"))
		{
			OnOff = true;
			return true;
		}
		if (Is("false") || Is("no") || Is("off"))
		{
			OnOff = false;
			return true;
		}
		char* End = nullptr;
		const long Number = std::strtol(Text.c_str(), &End, 10);
		if (End == Text.c_str())
		{
			return false;   // not a boolean word and no leading integer: rejected (UE would report false)
		}
		OnOff = Number != 0;
		return true;
	}

	bool FParse::Token(const char*& Stream, std::string& OutToken, const char Delimiter)
	{
		OutToken.clear();
		if (Stream == nullptr)
		{
			return false;
		}
		const auto IsSeparator = [Delimiter](const char C) { return C == ' ' || C == '\t' || (Delimiter != '\0' && C == Delimiter); };
		while (*Stream != '\0' && IsSeparator(*Stream))
		{
			++Stream;
		}
		if (*Stream == '"')
		{
			++Stream;
			while (*Stream != '\0' && *Stream != '"')
			{
				OutToken += *Stream++;
			}
			if (*Stream == '"')
			{
				++Stream;
			}
		}
		else
		{
			bool bInQuote = false;
			while (*Stream != '\0' && (bInQuote || !IsSeparator(*Stream)))
			{
				if (*Stream == '"')
				{
					bInQuote = !bInQuote;
				}
				OutToken += *Stream++;
			}
		}
		return !OutToken.empty();
	}
}
