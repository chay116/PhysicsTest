// ConfigCacheIni.cpp - minimal ini reader with UE's array operators (+ . - !) and per-line provenance.
// The syntax subset is described in Misc/ConfigCacheIni.h; the code is our own.
#include "Misc/ConfigCacheIni.h"

#include "Containers/StringConv.h"
#include "HAL/IConsoleManager.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>

namespace PhysicsTest
{
	namespace
	{
		char ToLowerAscii(const char C)
		{
			return (C >= 'A' && C <= 'Z') ? static_cast<char>(C - 'A' + 'a') : C;
		}

		bool EqualsNoCase(const std::string_view A, const std::string_view B)
		{
			if (A.size() != B.size())
			{
				return false;
			}
			for (size_t Index = 0; Index < A.size(); ++Index)
			{
				if (ToLowerAscii(A[Index]) != ToLowerAscii(B[Index]))
				{
					return false;
				}
			}
			return true;
		}

		bool IsSpace(const char C)
		{
			return C == ' ' || C == '\t' || C == '\r' || C == '\n' || C == '\v' || C == '\f';
		}

		std::string_view Trim(std::string_view Text)
		{
			while (!Text.empty() && IsSpace(Text.front()))
			{
				Text.remove_prefix(1);
			}
			while (!Text.empty() && IsSpace(Text.back()))
			{
				Text.remove_suffix(1);
			}
			return Text;
		}

		// "text" -> text with \" and \\ unescaped; anything else is returned trimmed and unchanged.
		std::string Unquote(const std::string_view Value)
		{
			if (Value.size() < 2 || Value.front() != '"' || Value.back() != '"')
			{
				return std::string(Value);
			}
			std::string Result;
			Result.reserve(Value.size() - 2);
			for (size_t Index = 1; Index + 1 < Value.size(); ++Index)
			{
				const char C = Value[Index];
				if (C == '\\' && Index + 2 < Value.size() && (Value[Index + 1] == '"' || Value[Index + 1] == '\\'))
				{
					Result += Value[Index + 1];
					++Index;
				}
				else
				{
					Result += C;
				}
			}
			return Result;
		}
	}

	// ----------------------------------------------------------------------------------------------------------------
	// FConfigSection
	// ----------------------------------------------------------------------------------------------------------------
	const FConfigValue* FConfigSection::Find(const std::string_view Key) const
	{
		for (const FEntry& Entry : Entries)
		{
			if (EqualsNoCase(Entry.Key, Key))
			{
				return &Entry.Value;
			}
		}
		return nullptr;
	}

	std::vector<const FConfigValue*> FConfigSection::MultiFind(const std::string_view Key) const
	{
		std::vector<const FConfigValue*> Result;
		for (const FEntry& Entry : Entries)
		{
			if (EqualsNoCase(Entry.Key, Key))
			{
				Result.push_back(&Entry.Value);
			}
		}
		return Result;
	}

	void FConfigSection::Apply(const EConfigLineOp Op, const std::string_view Key, const FConfigValue& Value)
	{
		const auto RemoveIf = [this, Key](const auto& Predicate)
		{
			std::erase_if(Entries, [Key, &Predicate](const FEntry& Entry) { return EqualsNoCase(Entry.Key, Key) && Predicate(Entry); });
		};
		switch (Op)
		{
		case EConfigLineOp::Set:
		{
			// Replace in place (keeps the key's first position, so the canonical order is stable across layers).
			const auto First = std::find_if(Entries.begin(), Entries.end(), [Key](const FEntry& Entry) { return EqualsNoCase(Entry.Key, Key); });
			if (First == Entries.end())
			{
				Entries.push_back(FEntry{ std::string(Key), Value });
			}
			else
			{
				const size_t Position = static_cast<size_t>(First - Entries.begin());
				RemoveIf([](const FEntry&) { return true; });
				Entries.insert(Entries.begin() + static_cast<std::ptrdiff_t>(Position), FEntry{ std::string(Key), Value });
			}
			break;
		}
		case EConfigLineOp::AddUnique:
			for (const FEntry& Entry : Entries)
			{
				if (EqualsNoCase(Entry.Key, Key) && Entry.Value.Value == Value.Value)
				{
					return;
				}
			}
			Entries.push_back(FEntry{ std::string(Key), Value });
			break;
		case EConfigLineOp::Add:
			Entries.push_back(FEntry{ std::string(Key), Value });
			break;
		case EConfigLineOp::Remove:
			RemoveIf([&Value](const FEntry& Entry) { return Entry.Value.Value == Value.Value; });
			break;
		case EConfigLineOp::Clear:
			RemoveIf([](const FEntry&) { return true; });
			break;
		}
	}

	// ----------------------------------------------------------------------------------------------------------------
	// FConfigFile
	// ----------------------------------------------------------------------------------------------------------------
	bool FConfigFile::ParseText(const std::string_view Text, std::string& OutError)
	{
		std::string_view Remaining = Text;
		if (Remaining.size() >= 3 && static_cast<unsigned char>(Remaining[0]) == 0xEF && static_cast<unsigned char>(Remaining[1]) == 0xBB
			&& static_cast<unsigned char>(Remaining[2]) == 0xBF)
		{
			Remaining.remove_prefix(3);   // UTF-8 BOM
		}
		std::string CurrentSection;
		int32 LineNumber = 0;
		const auto Fail = [this, &OutError, &LineNumber](const std::string& Message)
		{
			OutError = Name + ":" + std::to_string(LineNumber) + ": " + Message;
			return false;
		};
		size_t Position = 0;
		while (Position <= Remaining.size())
		{
			++LineNumber;
			const size_t End = Remaining.find('\n', Position);
			const std::string_view RawLine = Remaining.substr(Position, End == std::string_view::npos ? std::string_view::npos : End - Position);
			Position = End == std::string_view::npos ? Remaining.size() + 1 : End + 1;
			const std::string_view Line = Trim(RawLine);
			if (Line.empty() || Line.front() == ';' || Line.front() == '#')
			{
				continue;
			}
			if (Line.front() == '[')
			{
				if (Line.back() != ']' || Line.size() < 3)
				{
					return Fail("malformed section header '" + std::string(Line) + "'");
				}
				CurrentSection = std::string(Trim(Line.substr(1, Line.size() - 2)));
				FConfigLine Declaration;
				Declaration.Section = CurrentSection;
				Declaration.LineNumber = LineNumber;
				Lines.push_back(Declaration);
				ApplyLine(Declaration, Name);
				continue;
			}
			if (CurrentSection.empty())
			{
				return Fail("key/value line before the first [Section]: '" + std::string(Line) + "'");
			}
			FConfigLine Parsed;
			Parsed.Section = CurrentSection;
			Parsed.LineNumber = LineNumber;
			std::string_view Body = Line;
			switch (Body.front())
			{
			case '+': Parsed.Op = EConfigLineOp::AddUnique; Body.remove_prefix(1); break;
			case '.': Parsed.Op = EConfigLineOp::Add; Body.remove_prefix(1); break;
			case '-': Parsed.Op = EConfigLineOp::Remove; Body.remove_prefix(1); break;
			case '!': Parsed.Op = EConfigLineOp::Clear; Body.remove_prefix(1); break;
			default: break;
			}
			const size_t Equals = Body.find('=');
			if (Equals == std::string_view::npos && Parsed.Op != EConfigLineOp::Clear)
			{
				return Fail("expected Key=Value, got '" + std::string(Line) + "'");
			}
			Parsed.Key = std::string(Trim(Body.substr(0, Equals)));
			if (Parsed.Key.empty())
			{
				return Fail("empty key in '" + std::string(Line) + "'");
			}
			if (Equals != std::string_view::npos)
			{
				Parsed.Value = Unquote(Trim(Body.substr(Equals + 1)));
			}
			Lines.push_back(Parsed);
			ApplyLine(Parsed, Name);
		}
		return true;
	}

	bool FConfigFile::LoadFile(const std::string& Utf8Path, std::string& OutError)
	{
		std::ifstream Stream(StringConv::Utf8ToPath(Utf8Path), std::ios::binary);
		if (!Stream)
		{
			OutError = "cannot open '" + Utf8Path + "'";
			return false;
		}
		const std::string Text((std::istreambuf_iterator<char>(Stream)), std::istreambuf_iterator<char>());
		if (Name.empty())
		{
			Name = Utf8Path;
		}
		return ParseText(Text, OutError);
	}

	bool FConfigFile::LoadFile(const std::string& Utf8Path, FConfigFile& OutFile, std::string& OutError)
	{
		OutFile = FConfigFile();
		return OutFile.LoadFile(Utf8Path, OutError);
	}

	void FConfigFile::Combine(const FConfigFile& Other)
	{
		for (const FConfigLine& Line : Other.Lines)
		{
			Lines.push_back(Line);
			ApplyLine(Line, Other.Name);
		}
	}

	void FConfigFile::AddLine(const FConfigLine& Line)
	{
		Lines.push_back(Line);
		ApplyLine(Line, Name);
	}

	void FConfigFile::ApplyLine(const FConfigLine& Line, const std::string& SourceName)
	{
		FConfigSection* Section = nullptr;
		for (auto& Entry : Sections)
		{
			if (EqualsNoCase(Entry.first, Line.Section))   // the first spelling of a section is kept
			{
				Section = &Entry.second;
				break;
			}
		}
		if (Section == nullptr)
		{
			Sections.emplace_back(Line.Section, FConfigSection());
			Section = &Sections.back().second;
		}
		if (Line.Key.empty())
		{
			return;   // section declaration only
		}
		Section->Apply(Line.Op, Line.Key, FConfigValue{ Line.Value, SourceName, Line.LineNumber });
	}

	const FConfigSection* FConfigFile::FindSection(const std::string_view Section) const
	{
		for (const auto& Entry : Sections)
		{
			if (EqualsNoCase(Entry.first, Section))
			{
				return &Entry.second;
			}
		}
		return nullptr;
	}

	std::vector<std::string> FConfigFile::GetSectionNames() const
	{
		std::vector<std::string> Names;
		Names.reserve(Sections.size());
		for (const auto& Entry : Sections)
		{
			Names.push_back(Entry.first);
		}
		return Names;
	}

	bool FConfigFile::GetString(const std::string_view Section, const std::string_view Key, std::string& OutValue) const
	{
		const FConfigSection* Found = FindSection(Section);
		const FConfigValue* Value = Found != nullptr ? Found->Find(Key) : nullptr;
		if (Value == nullptr)
		{
			return false;
		}
		OutValue = Value->Value;
		return true;
	}

	bool FConfigFile::GetBool(const std::string_view Section, const std::string_view Key, bool& OutValue) const
	{
		std::string Text;
		return GetString(Section, Key, Text) && ConsoleVariablePrivate::ParseBool(Text, OutValue);
	}

	bool FConfigFile::GetInt(const std::string_view Section, const std::string_view Key, int32& OutValue) const
	{
		std::string Text;
		return GetString(Section, Key, Text) && ConsoleVariablePrivate::ParseInt(Text, OutValue);
	}

	bool FConfigFile::GetFloat(const std::string_view Section, const std::string_view Key, float& OutValue) const
	{
		std::string Text;
		return GetString(Section, Key, Text) && ConsoleVariablePrivate::ParseFloat(Text, OutValue);
	}

	bool FConfigFile::GetDouble(const std::string_view Section, const std::string_view Key, double& OutValue) const
	{
		std::string Text;
		if (!GetString(Section, Key, Text))
		{
			return false;
		}
		std::string_view View = Trim(Text);
		if (!View.empty() && View.front() == '+')
		{
			View.remove_prefix(1);
		}
		double Value = 0.0;
		const auto [Ptr, Error] = std::from_chars(View.data(), View.data() + View.size(), Value);
		if (View.empty() || Error != std::errc() || Ptr != View.data() + View.size() || !std::isfinite(Value))
		{
			return false;
		}
		OutValue = Value;
		return true;
	}

	bool FConfigFile::GetArray(const std::string_view Section, const std::string_view Key, std::vector<std::string>& OutValues) const
	{
		OutValues.clear();
		const FConfigSection* Found = FindSection(Section);
		if (Found == nullptr)
		{
			return false;
		}
		for (const FConfigValue* Value : Found->MultiFind(Key))
		{
			OutValues.push_back(Value->Value);
		}
		return !OutValues.empty();
	}

	std::string FConfigFile::ToCanonicalString() const
	{
		std::ostringstream Out;
		for (const auto& [SectionName, Section] : Sections)
		{
			Out << '[' << SectionName << "]\n";
			for (const FConfigSection::FEntry& Entry : Section.GetEntries())
			{
				Out << Entry.Key << '=' << Entry.Value.Value << '\n';
			}
		}
		return Out.str();
	}
}
