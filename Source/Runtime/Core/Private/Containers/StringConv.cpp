// StringConv.cpp - UTF-16 <-> UTF-8 through the Win32 conversion functions (CP_UTF8 only, never the ANSI code page).
#include "Containers/StringConv.h"

#include <Windows.h>

#include <climits>

namespace PhysicsTest::StringConv
{
	std::string WideToUtf8(const std::wstring_view Wide)
	{
		if (Wide.empty())
		{
			return {};
		}
		const int WideLength = Wide.size() > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(Wide.size());
		const int Bytes = ::WideCharToMultiByte(CP_UTF8, 0, Wide.data(), WideLength, nullptr, 0, nullptr, nullptr);
		if (Bytes <= 0)
		{
			return {};
		}
		std::string Result(static_cast<size_t>(Bytes), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, Wide.data(), WideLength, Result.data(), Bytes, nullptr, nullptr);
		return Result;
	}

	std::wstring Utf8ToWide(const std::string_view Utf8)
	{
		if (Utf8.empty())
		{
			return {};
		}
		const int Utf8Length = Utf8.size() > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(Utf8.size());
		const int Chars = ::MultiByteToWideChar(CP_UTF8, 0, Utf8.data(), Utf8Length, nullptr, 0);
		if (Chars <= 0)
		{
			return {};
		}
		std::wstring Result(static_cast<size_t>(Chars), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, Utf8.data(), Utf8Length, Result.data(), Chars);
		return Result;
	}

	std::filesystem::path Utf8ToPath(const std::string_view Utf8)
	{
		return std::filesystem::path(Utf8ToWide(Utf8));
	}

	std::string PathToUtf8(const std::filesystem::path& Path)
	{
		return WideToUtf8(Path.native());
	}
}
