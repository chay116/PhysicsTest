// ConfigCacheIni.h - ini files (UE: Core/Public/Misc/ConfigCacheIni.h: FConfigFile, FConfigSection), minimal (DL18,
// S5). Used by the profile resolver (PhysicsCore Physics/PhysicsProfile.h). UE's FConfigCacheIni / GConfig (the named
// file cache) is not mirrored: the harness layers files explicitly.
//
// Syntax (UE subset):
//   [Section]              section header. Section names and keys compare case-insensitively, as in UE (FString keys
//                          of TMap<FString, FConfigSection>, ConfigCacheIni.h:502, :574 [V]; FName keys); a section
//                          keeps the spelling it was first written with
//   Key=Value              set: replaces every current value of Key (inside one file and when a file is layered on
//                          top of another)
//   +Key=Value             add unless the same value is already there (UE AddUnique, arrays)
//   .Key=Value             add, duplicates allowed (UE Add)
//   -Key=Value             remove that value
//   !Key=                  remove every value of Key (UE: ClearArray)
//   ; comment  # comment   whole-line comments only (UE has no inline comments either)
//   Key="text"             surrounding double quotes are removed; \" and \\ inside are unescaped
// Not mirrored: UE's hierarchy discovery (Base/Default/Saved), platform sections, @-struct-key array semantics,
// line continuation and per-object config. The harness layers files explicitly (FConfigFile::Combine) and keeps the
// raw operations of every file so a resolver can record where each value came from.
#pragma once

#include "CoreTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace PhysicsTest
{
	enum class EConfigLineOp : uint8
	{
		Set,         // Key=Value
		AddUnique,   // +Key=Value
		Add,         // .Key=Value
		Remove,      // -Key=Value
		Clear,       // !Key
	};

	// One key/value line as written, with its position (used for Origin tracking).
	struct FConfigLine
	{
		std::string Section;
		std::string Key;
		std::string Value;
		EConfigLineOp Op = EConfigLineOp::Set;
		int32 LineNumber = 0;
	};

	// UE: FConfigValue - one value and the line that produced it.
	struct FConfigValue
	{
		std::string Value;
		std::string SourceName;   // file display name
		int32 LineNumber = 0;
	};

	// Ordered multi-map Key -> values (UE: FConfigSection). Key comparison is case-insensitive.
	class FConfigSection
	{
	public:
		struct FEntry
		{
			std::string Key;
			FConfigValue Value;
		};

		// First value of Key, or nullptr.
		[[nodiscard]] const FConfigValue* Find(std::string_view Key) const;
		[[nodiscard]] std::vector<const FConfigValue*> MultiFind(std::string_view Key) const;
		[[nodiscard]] bool Contains(std::string_view Key) const { return Find(Key) != nullptr; }
		[[nodiscard]] const std::vector<FEntry>& GetEntries() const { return Entries; }
		[[nodiscard]] bool IsEmpty() const { return Entries.empty(); }

		void Apply(EConfigLineOp Op, std::string_view Key, const FConfigValue& Value);

	private:
		std::vector<FEntry> Entries;
	};

	class FConfigFile
	{
	public:
		FConfigFile() = default;
		explicit FConfigFile(std::string InName) : Name(std::move(InName)) {}

		// Parses Text and applies it to this file. Returns false with OutError ("<name>:<line>: ...") on a syntax error.
		bool ParseText(std::string_view Text, std::string& OutError);
		// Reads a UTF-8 file (optional BOM). The path is UTF-8; Name defaults to the path as given.
		bool LoadFile(const std::string& Utf8Path, std::string& OutError);
		[[nodiscard]] static bool LoadFile(const std::string& Utf8Path, FConfigFile& OutFile, std::string& OutError);

		// Applies Other's raw lines (in order) on top of this file (UE: FConfigFile::Combine). Lines keep Other's
		// SourceName / LineNumber.
		void Combine(const FConfigFile& Other);
		// Records Line as one of this file's own lines (e.g. built from a command line) and applies it.
		void AddLine(const FConfigLine& Line);
		// Applies Line without recording it (SourceName / LineNumber are kept on the value).
		void ApplyLine(const FConfigLine& Line, const std::string& SourceName);

		[[nodiscard]] const FConfigSection* FindSection(std::string_view Section) const;
		[[nodiscard]] std::vector<std::string> GetSectionNames() const;
		[[nodiscard]] const std::vector<FConfigLine>& GetLines() const { return Lines; }

		bool GetString(std::string_view Section, std::string_view Key, std::string& OutValue) const;
		bool GetBool(std::string_view Section, std::string_view Key, bool& OutValue) const;
		bool GetInt(std::string_view Section, std::string_view Key, int32& OutValue) const;
		bool GetFloat(std::string_view Section, std::string_view Key, float& OutValue) const;
		bool GetDouble(std::string_view Section, std::string_view Key, double& OutValue) const;
		// Every value of Key in order (arrays written with + / .).
		bool GetArray(std::string_view Section, std::string_view Key, std::vector<std::string>& OutValues) const;

		// Canonical text: sections and keys in insertion order, one Key=Value line per value (input of SHA-256 hashes).
		[[nodiscard]] std::string ToCanonicalString() const;

		std::string Name;   // display name, e.g. "Config/BasePhysicsTest.ini"

	private:
		std::vector<std::pair<std::string, FConfigSection>> Sections;   // insertion order
		std::vector<FConfigLine> Lines;                                  // raw operations, in file order
	};
}
