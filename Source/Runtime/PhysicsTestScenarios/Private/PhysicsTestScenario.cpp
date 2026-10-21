// PhysicsTestScenario.cpp - FScenarioRegistry, parameter resolution and scene_sha256 (see PhysicsTestScenario.h).
#include "PhysicsTestScenario.h"

#include "Hash/Sha256.h"
#include "Logging/LogMacros.h"
#include "Misc/AssertionMacros.h"
#include "Physics/PhysicsProfile.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"

#include <algorithm>
#include <charconv>
#include <cstdio>

DEFINE_LOG_CATEGORY_STATIC(LogPhysicsTestScenario, Log, All);

namespace PhysicsTest
{
	namespace
	{
		std::string ToLowerAscii(std::string Text)
		{
			for (char& C : Text)
			{
				C = (C >= 'A' && C <= 'Z') ? static_cast<char>(C - 'A' + 'a') : C;
			}
			return Text;
		}

		void AppendJsonString(std::string& Out, const std::string& Text)
		{
			Out += '"';
			for (const char C : Text)
			{
				if (C == '"' || C == '\\')
				{
					Out += '\\';
					Out += C;
				}
				else if (static_cast<unsigned char>(C) < 0x20)
				{
					char Escaped[8];
					std::snprintf(Escaped, sizeof(Escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(C)));
					Out += Escaped;
				}
				else
				{
					Out += C;
				}
			}
			Out += '"';
		}

		// Int32 parameter text: optional '-', then decimal digits only (no '+', blanks, hex or trailing text), in range.
		bool ParseInt32Param(const std::string& Text, int32& OutValue)
		{
			const char* const Begin = Text.data();
			const char* const End = Begin + Text.size();
			const char* Digits = (Begin != End && *Begin == '-') ? Begin + 1 : Begin;
			if (Digits == End || !std::all_of(Digits, End, [](const char C) { return C >= '0' && C <= '9'; }))
			{
				return false;
			}
			int32 Value = 0;
			const std::from_chars_result Result = std::from_chars(Begin, End, Value, 10);
			if (Result.ec != std::errc() || Result.ptr != End)
			{
				return false;   // out of range
			}
			OutValue = Value;
			return true;
		}
	}

	const char* ToString(const EScenarioKind Kind)
	{
		switch (Kind)
		{
		case EScenarioKind::Gate: return "Gate";
		case EScenarioKind::Overhead: return "Overhead";
		case EScenarioKind::Behavior: return "Behavior";
		case EScenarioKind::Perf: return "Perf";
		case EScenarioKind::Extended: return "Extended";
		}
		return "?";
	}

	const char* ToString(const EScenarioParamType Type)
	{
		switch (Type)
		{
		case EScenarioParamType::Text: return "text";
		case EScenarioParamType::Int32: return "int32";
		}
		return "?";
	}

	FBodyInstance FScenarioContext::MakeBodyTemplate() const
	{
		FBodyInstance Template;
		if (Config != nullptr)
		{
			FProfileResolver::ApplyBodyInstanceDefaults(*Config, Template);
		}
		return Template;
	}

	FPhysicalMaterial FScenarioContext::MakeMaterialTemplate() const
	{
		FPhysicalMaterial Template;
		if (Config != nullptr)
		{
			FProfileResolver::ApplyMaterialDefaults(*Config, Template);
		}
		return Template;
	}

	const std::string& FScenarioContext::GetParam(const char* Name) const
	{
		static const std::string Empty;
		const auto Found = Params.find(Name);
		return Found != Params.end() ? Found->second : Empty;
	}

	bool IPhysicsTestScenario::ResolveParams(const FScenarioParams& Overrides, FScenarioParams& OutResolved, std::string& OutError) const
	{
		OutResolved.clear();
		const std::vector<FScenarioParamDesc> Descs = GetParamDescs();
		// Checks one value against its declaration and returns its canonical text.
		const auto Check = [this, &OutError](const FScenarioParamDesc& Desc, const std::string& Value, std::string& OutCanonical) -> bool
		{
			if (Desc.Type == EScenarioParamType::Int32)
			{
				int32 Number = 0;
				if (!ParseInt32Param(Value, Number))
				{
					OutError = std::string("scenario ") + GetName() + ": " + Desc.Name + "=" + Value + " is not a decimal 32-bit integer";
					return false;
				}
				OutCanonical = std::to_string(Number);
				return true;
			}
			if (!Desc.AllowedValues.empty() && std::find(Desc.AllowedValues.begin(), Desc.AllowedValues.end(), Value) == Desc.AllowedValues.end())
			{
				OutError = std::string("scenario ") + GetName() + ": " + Desc.Name + "=" + Value + " is not one of";
				for (const std::string& Allowed : Desc.AllowedValues)
				{
					OutError += " " + Allowed;
				}
				return false;
			}
			OutCanonical = Value;
			return true;
		};
		for (const FScenarioParamDesc& Desc : Descs)
		{
			std::string Canonical;
			if (!Check(Desc, Desc.Default, Canonical))
			{
				OutError = "(declared default) " + OutError;   // a scenario bug, reported like a bad override
				return false;
			}
			OutResolved[Desc.Name] = Canonical;
		}
		for (const auto& [Name, Value] : Overrides)
		{
			const auto Desc = std::find_if(Descs.begin(), Descs.end(), [&Name](const FScenarioParamDesc& D) { return ToLowerAscii(D.Name) == ToLowerAscii(Name); });
			if (Desc == Descs.end())
			{
				OutError = std::string("scenario ") + GetName() + " has no parameter '" + Name + "'";
				return false;
			}
			std::string Canonical;
			if (!Check(*Desc, Value, Canonical))
			{
				return false;
			}
			OutResolved[Desc->Name] = Canonical;
		}
		return true;
	}

	int32 IPhysicsTestScenario::GetSeed(const FScenarioParams& Resolved) const
	{
		const auto Found = Resolved.find("Seed");
		int32 Seed = 0;
		if (Found != Resolved.end() && ParseInt32Param(Found->second, Seed))
		{
			return Seed;
		}
		checkf(Found == Resolved.end(), "scenario %s: Seed '%s' was not resolved (ResolveParams validates it)", GetName(), Found->second.c_str());
		return GetDefaultSeed();
	}

	std::string IPhysicsTestScenario::MakeCanonicalJson(const FScenarioParams& Resolved) const
	{
		// Keys in a fixed order, parameters sorted by name (std::map), every parameter value a string: one text per scene.
		// The seed is always there (M4, M3 review): a scene without a Seed parameter still draws from FRandomStream with
		// GetDefaultSeed(), and a change of that default must change scene_sha256 (02 12.3: name, version, params, seed).
		std::string Json = "{\"name\":";
		AppendJsonString(Json, GetName());
		Json += ",\"params\":{";
		bool bFirst = true;
		for (const auto& [Name, Value] : Resolved)
		{
			Json += bFirst ? "" : ",";
			bFirst = false;
			AppendJsonString(Json, Name);
			Json += ':';
			AppendJsonString(Json, Value);
		}
		Json += "},\"seed\":" + std::to_string(GetSeed(Resolved)) + ",\"version\":" + std::to_string(GetVersion()) + "}";
		return Json;
	}

	std::string IPhysicsTestScenario::ComputeSceneSha256(const FScenarioParams& Resolved) const
	{
		return FSHA256::HashString(MakeCanonicalJson(Resolved));
	}

	FScenarioRegistry& FScenarioRegistry::Get()
	{
		static FScenarioRegistry Registry;
		return Registry;
	}

	void FScenarioRegistry::Register(IPhysicsTestScenario* Scenario)
	{
		Scenarios.push_back(Scenario);
	}

	IPhysicsTestScenario* FScenarioRegistry::Find(const std::string& Name) const
	{
		const std::string Wanted = ToLowerAscii(Name);
		for (IPhysicsTestScenario* Scenario : Scenarios)
		{
			if (ToLowerAscii(Scenario->GetName()) == Wanted)
			{
				return Scenario;
			}
		}
		return nullptr;
	}

	std::vector<IPhysicsTestScenario*> FScenarioRegistry::GetAll() const
	{
		std::vector<IPhysicsTestScenario*> Sorted = Scenarios;
		std::sort(Sorted.begin(), Sorted.end(), [](const IPhysicsTestScenario* A, const IPhysicsTestScenario* B) { return std::string(A->GetName()) < B->GetName(); });
		return Sorted;
	}

	void FScenarioRegistry::CheckNotEmpty(const char* Program)
	{
		if (Get().Scenarios.empty())
		{
			UE_LOG(LogPhysicsTestScenario, Fatal, "%s: no scenario is registered (link PT::Scenarios with WHOLE_ARCHIVE, 02_Architecture 4.5)", Program);
		}
	}
}
