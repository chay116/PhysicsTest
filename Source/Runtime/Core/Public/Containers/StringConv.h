// StringConv.h - UTF-16 (Windows wide) <-> UTF-8 conversion (UE: Core/Public/Containers/StringConv.h, the
// TCHAR_TO_UTF8 / UTF8_TO_TCHAR family), minimal (DL18).
// The harness keeps every string in UTF-8 (sources are compiled with /utf-8). Windows hands command lines and paths
// over as UTF-16, so programs convert at the boundary (wmain, file paths) and never go through the ANSI code page
// (cp949 on Korean Windows), which cannot represent arbitrary Unicode paths.
#pragma once

#include "CoreTypes.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace PhysicsTest::StringConv
{
	// Invalid UTF-16 (lone surrogates) is replaced by U+FFFD, as WideCharToMultiByte does without WC_ERR_INVALID_CHARS.
	[[nodiscard]] std::string WideToUtf8(std::wstring_view Wide);
	// Invalid UTF-8 is replaced by U+FFFD (MultiByteToWideChar without MB_ERR_INVALID_CHARS).
	[[nodiscard]] std::wstring Utf8ToWide(std::string_view Utf8);

	// std::filesystem::path(std::string) decodes with the ANSI code page on Windows; these two never do.
	[[nodiscard]] std::filesystem::path Utf8ToPath(std::string_view Utf8);
	[[nodiscard]] std::string PathToUtf8(const std::filesystem::path& Path);
}
