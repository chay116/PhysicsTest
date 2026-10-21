// Tolerances.cpp - FTolerances: Config/Golden/tolerances.json (schema physicstest.tolerances/1, see PhysicsGates.h).
//
//   { "schema": "physicstest.tolerances/1", "version": 1,
//     "gates": { "G1": { "z60_abs_cm": { "default": 2e-3, "unit": "cm", "reason": "...",
//                                        "overrides": [ { "engine": "Null", "value": 1e-9, "reason": "..." } ] }, ... },
//                "G3": { "allowed_skip": [ { "engine": "Null", "reason": "..." } ], ... },
//                "G9": { ..., "readback": { "*": { "world.gravity": { "expect": "profile:gravity_z" } },
//                                         "PhysX.SdkDefault": { ... }, "PhysX": { ... } } } } }
//
// Every check, every override and every allowed skip carries a reason; a missing reason is a load error, so nothing is
// registered without one. An override may say "registered": "pre-run" (the default) or "post-run" (found on a run and
// registered after its mechanism was verified; the gate's note says so). A post-run override needs "sign_off": "pending"
// until the lead accepts it, then who accepted it and when (M6 round 1); a pre-run override has none.
#include "Gates/PhysicsGates.h"

#include "Containers/StringConv.h"
#include "Hash/Sha256.h"
#include "Logging/LogMacros.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <nlohmann/json.hpp>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

DEFINE_LOG_CATEGORY_STATIC(LogPhysicsGates, Log, All);

namespace PhysicsTest::Gates
{
	namespace
	{
		using FJson = nlohmann::json;

		bool ReadReason(const FJson& Object, const std::string& Where, std::string& OutReason, std::string& OutError)
		{
			const auto Found = Object.find("reason");
			if (Found == Object.end() || !Found->is_string() || Found->get<std::string>().empty())
			{
				OutError = Where + ": every registered value needs a non-empty \"reason\"";
				return false;
			}
			OutReason = Found->get<std::string>();
			return true;
		}

		bool ParseReadbackRule(const FJson& Object, const std::string& Where, FReadbackRule& Out, std::string& OutError)
		{
			if (!Object.is_object())
			{
				OutError = Where + ": a readback rule is an object";
				return false;
			}
			if (const auto Unchecked = Object.find("unchecked"); Unchecked != Object.end())
			{
				if (!Unchecked->is_string() || Unchecked->get<std::string>().empty())
				{
					OutError = Where + ": \"unchecked\" needs the reason as a non-empty string";
					return false;
				}
				Out.Unchecked = Unchecked->get<std::string>();
				return true;
			}
			const auto Expect = Object.find("expect");
			if (Expect == Object.end() || !Expect->is_string())
			{
				OutError = Where + ": a readback rule needs \"expect\" (or \"unchecked\": \"<reason>\")";
				return false;
			}
			Out.Expect = Expect->get<std::string>();
			if (const auto Fallback = Object.find("fallback"); Fallback != Object.end() && Fallback->is_string())
			{
				Out.Fallback = Fallback->get<std::string>();
			}
			// M7 (M6 review): malformed entries are load errors, never a json::type_error that ends the program.
			if (const auto Options = Object.find("options"); Options != Object.end())
			{
				if (!Options->is_array())
				{
					OutError = Where + ": \"options\" is a list of variant option names (strings)";
					return false;
				}
				for (const FJson& Option : *Options)
				{
					if (!Option.is_string())
					{
						OutError = Where + ": options/flags entries must be strings (\"options\" holds " + std::string(Option.type_name()) + ")";
						return false;
					}
					Out.Options.push_back(Option.get<std::string>());
				}
			}
			if (const auto Flags = Object.find("flags"); Flags != Object.end())
			{
				if (!Flags->is_object())
				{
					OutError = Where + ": \"flags\" is an object of \"<name>\": \"option:<Key>\" | \"profile:<name>\"";
					return false;
				}
				for (auto It = Flags->begin(); It != Flags->end(); ++It)
				{
					if (!It.value().is_string())
					{
						OutError = Where + ": options/flags entries must be strings (\"flags." + It.key() + "\" holds " + std::string(It.value().type_name()) + ")";
						return false;
					}
					Out.Flags[It.key()] = It.value().get<std::string>();
				}
			}
			if (const auto Rel = Object.find("rel"); Rel != Object.end() && Rel->is_number())
			{
				Out.Rel = Rel->get<double>();
			}
			if (const auto Abs = Object.find("abs"); Abs != Object.end() && Abs->is_number())
			{
				Out.Abs = Abs->get<double>();
			}
			if (const auto Reason = Object.find("reason"); Reason != Object.end() && Reason->is_string())
			{
				Out.Reason = Reason->get<std::string>();
			}
			if (const auto Scene = Object.find("scene"); Scene != Object.end())
			{
				// One scenario name or a list of them (M7 fix round 0).
				const auto IsName = [](const FJson& Name) { return Name.is_string() && !Name.get<std::string>().empty(); };
				const bool bList = Scene->is_array() && !Scene->empty() && std::all_of(Scene->begin(), Scene->end(), IsName);
				if (!IsName(*Scene) && !bList)
				{
					OutError = Where + ": \"scene\" names the scenario (ID.Name) whose geometry the expectation holds for, or a non-empty list of them";
					return false;
				}
				if (bList)
				{
					for (const FJson& Name : *Scene)
					{
						Out.Scenes.push_back(Name.get<std::string>());
					}
				}
				else
				{
					Out.Scenes.push_back(Scene->get<std::string>());
				}
			}
			const bool bNeedsReason = Out.Expect.rfind("const:", 0) == 0 || Out.Expect.rfind("min:", 0) == 0;
			if (bNeedsReason && Out.Reason.empty())
			{
				OutError = Where + ": a \"const:\" / \"min:\" expectation needs the \"reason\" for its value";
				return false;
			}
			return true;
		}

		bool MatchesOrAny(const std::string& Pattern, std::string_view Value)
		{
			return Pattern.empty() || Pattern == Value;
		}
	}

	std::filesystem::path GetTolerancesPath(const std::filesystem::path& ConfigDir)
	{
		return ConfigDir / "Golden" / "tolerances.json";
	}

	bool FTolerances::LoadFile(const std::filesystem::path& Path, std::string& OutError)
	{
		std::ifstream File(Path, std::ios::binary);
		if (!File)
		{
			OutError = "cannot read " + StringConv::PathToUtf8(Path);
			return false;
		}
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		return LoadText(Buffer.str(), StringConv::PathToUtf8(Path), OutError);
	}

	bool FTolerances::LoadText(const std::string_view Text, const std::string& DisplayName, std::string& OutError)
	{
		// Every field is type-checked where it is read; a field of an unexpected JSON type that an accessor still meets
		// (Item.value("engine", ...) on a number, a readback scope that is not an object) is reported as a load error too,
		// so a malformed file never ends the program (M7, M6 review).
		try
		{
			return LoadTextChecked(Text, DisplayName, OutError);
		}
		catch (const nlohmann::json::exception& Exception)
		{
			Entries.clear();
			ReadbackRules.clear();
			AllowedSkips.clear();
			OutError = DisplayName + ": malformed entry (" + Exception.what() + ")";
			return false;
		}
	}

	bool FTolerances::LoadTextChecked(const std::string_view Text, const std::string& DisplayName, std::string& OutError)
	{
		Entries.clear();
		ReadbackRules.clear();
		AllowedSkips.clear();
		SourceName = DisplayName;
		Sha256 = FSHA256::HashString(Text);
		const FJson Root = FJson::parse(Text.begin(), Text.end(), nullptr, false);
		if (Root.is_discarded() || !Root.is_object())
		{
			OutError = DisplayName + ": not a JSON object";
			return false;
		}
		if (Root.value("schema", std::string()) != "physicstest.tolerances/1")
		{
			OutError = DisplayName + ": schema must be \"physicstest.tolerances/1\"";
			return false;
		}
		Version = Root.value("version", 0);
		const auto GatesIt = Root.find("gates");
		if (GatesIt == Root.end() || !GatesIt->is_object())
		{
			OutError = DisplayName + ": \"gates\" object missing";
			return false;
		}
		for (auto Gate = GatesIt->begin(); Gate != GatesIt->end(); ++Gate)
		{
			if (!Gate.value().is_object())
			{
				OutError = DisplayName + ": gates." + Gate.key() + " is not an object";
				return false;
			}
			for (auto Check = Gate.value().begin(); Check != Gate.value().end(); ++Check)
			{
				const std::string Where = DisplayName + ": gates." + Gate.key() + "." + Check.key();
				if (Check.key() == "description")
				{
					continue;
				}
				if (Check.key() == "allowed_skip")
				{
					if (!Check.value().is_array())
					{
						OutError = Where + ": a list of { \"engine\", \"variant\", \"profile\", \"reason\" }";
						return false;
					}
					for (const FJson& Item : Check.value())
					{
						FAllowedSkip Skip;
						Skip.Engine = Item.value("engine", std::string());
						Skip.Variant = Item.value("variant", std::string());
						Skip.Profile = Item.value("profile", std::string());
						if (Skip.Engine.empty())
						{
							OutError = Where + ": an allowed skip needs \"engine\"";
							return false;
						}
						if (!ReadReason(Item, Where + " for " + Skip.Engine, Skip.Reason, OutError))
						{
							return false;
						}
						AllowedSkips[Gate.key()].push_back(std::move(Skip));
					}
					continue;
				}
				if (Check.key() == "readback")
				{
					if (!Check.value().is_object())
					{
						OutError = Where + ": an object of scopes (\"*\", \"<Engine>\", \"<Engine>.<Variant>\")";
						return false;
					}
					for (auto Scope = Check.value().begin(); Scope != Check.value().end(); ++Scope)
					{
						if (!Scope.value().is_object())
						{
							OutError = Where + "." + Scope.key() + ": an object of \"<effective key>\": { rule }";
							return false;
						}
						for (auto Key = Scope.value().begin(); Key != Scope.value().end(); ++Key)
						{
							FReadbackRule Rule;
							const std::string RuleWhere = Where + "." + Scope.key() + "." + Key.key();
							if (!ParseReadbackRule(Key.value(), RuleWhere, Rule, OutError))
							{
								return false;
							}
							// "by_scene": { "<scene>": { rule } } - another scene's own expectation of the same key (M7 fix round 0).
							if (const auto ByScene = Key.value().find("by_scene"); ByScene != Key.value().end())
							{
								if (!ByScene->is_object() || ByScene->empty())
								{
									OutError = RuleWhere + ": \"by_scene\" is an object of \"<scene>\": { rule }";
									return false;
								}
								for (auto Entry = ByScene->begin(); Entry != ByScene->end(); ++Entry)
								{
									FReadbackRule SceneRule;
									const std::string SceneWhere = RuleWhere + ".by_scene." + Entry.key();
									if (Entry.key().empty() || !Entry.value().is_object() || Entry.value().contains("by_scene") || Entry.value().contains("scene"))
									{
										OutError = SceneWhere + ": a by_scene entry is a rule of a named scene, without its own \"scene\" or \"by_scene\"";
										return false;
									}
									if (!ParseReadbackRule(Entry.value(), SceneWhere, SceneRule, OutError))
									{
										return false;
									}
									SceneRule.Scenes = { Entry.key() };
									ReadbackRules[Scope.key()][Key.key() + "@" + Entry.key()] = std::move(SceneRule);
								}
							}
							ReadbackRules[Scope.key()][Key.key()] = std::move(Rule);
						}
					}
					continue;
				}
				const FJson& Object = Check.value();
				if (!Object.is_object() || !Object.contains("default") || !Object["default"].is_number())
				{
					OutError = Where + ": needs a numeric \"default\"";
					return false;
				}
				FToleranceEntry Entry;
				Entry.Default = Object["default"].get<double>();
				Entry.Unit = Object.value("unit", std::string());
				if (!ReadReason(Object, Where, Entry.Reason, OutError))
				{
					return false;
				}
				if (const auto Overrides = Object.find("overrides"); Overrides != Object.end())
				{
					if (!Overrides->is_array())
					{
						OutError = Where + ": \"overrides\" is a list of { \"engine\", \"value\", \"reason\", ... }";
						return false;
					}
					for (const FJson& Item : *Overrides)
					{
						FToleranceOverride Override;
						Override.Engine = Item.value("engine", std::string());
						Override.Variant = Item.value("variant", std::string());
						Override.Profile = Item.value("profile", std::string());
						if (Override.Engine.empty() || !Item.contains("value") || !Item["value"].is_number())
						{
							OutError = Where + ": an override needs \"engine\" and a numeric \"value\"";
							return false;
						}
						Override.Value = Item["value"].get<double>();
						if (!ReadReason(Item, Where + " override for " + Override.Engine, Override.Reason, OutError))
						{
							return false;
						}
						const std::string Registered = Item.value("registered", std::string("pre-run"));
						if (Registered != "pre-run" && Registered != "post-run")
						{
							OutError = Where + " override for " + Override.Engine + ": \"registered\" is \"pre-run\" or \"post-run\"";
							return false;
						}
						Override.bPostRun = Registered == "post-run";
						const auto SignOff = Item.find("sign_off");
						if (Override.bPostRun && (SignOff == Item.end() || !SignOff->is_string() || SignOff->get<std::string>().empty()))
						{
							OutError = Where + " override for " + Override.Engine + ": a post-run registration needs \"sign_off\" (\"pending\" until the lead accepts it)";
							return false;
						}
						if (!Override.bPostRun && SignOff != Item.end())
						{
							OutError = Where + " override for " + Override.Engine + ": \"sign_off\" belongs to post-run registrations only";
							return false;
						}
						Override.SignOff = Override.bPostRun ? SignOff->get<std::string>() : std::string();
						Entry.Overrides.push_back(std::move(Override));
					}
				}
				Entries[Gate.key() + "." + Check.key()] = std::move(Entry);
			}
		}
		return true;
	}

	bool FTolerances::Has(const std::string_view Gate, const std::string_view Check) const
	{
		return Entries.find(std::string(Gate) + "." + std::string(Check)) != Entries.end();
	}

	const FToleranceOverride* FTolerances::FindOverride(const std::string_view Gate, const std::string_view Check, const std::string_view Engine,
		const std::string_view Variant, const std::string_view Profile) const
	{
		const std::string Key = std::string(Gate) + "." + std::string(Check);
		const auto Found = Entries.find(Key);
		if (Found == Entries.end())
		{
			UE_LOG(LogPhysicsGates, Fatal, "tolerance %s is not registered in %s (every gate value is registered before the run, 02 14.3)", Key.c_str(),
				SourceName.c_str());
			return nullptr;
		}
		// Most specific match: each of engine / variant / profile that the override names must match; the score counts them.
		int32 BestScore = -1;
		const FToleranceOverride* Best = nullptr;
		for (const FToleranceOverride& Override : Found->second.Overrides)
		{
			if (Override.Engine != Engine || !MatchesOrAny(Override.Variant, Variant) || !MatchesOrAny(Override.Profile, Profile))
			{
				continue;
			}
			const int32 Score = 1 + (Override.Variant.empty() ? 0 : 2) + (Override.Profile.empty() ? 0 : 1);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = &Override;
			}
		}
		return Best;
	}

	double FTolerances::Get(const std::string_view Gate, const std::string_view Check, const std::string_view Engine, const std::string_view Variant,
		const std::string_view Profile, std::string* OutReason, bool* bOutPostRun, std::string* OutSignOff) const
	{
		const FToleranceOverride* Best = FindOverride(Gate, Check, Engine, Variant, Profile);
		const auto Found = Entries.find(std::string(Gate) + "." + std::string(Check));
		if (Found == Entries.end())
		{
			return 0.0;   // FindOverride reported it (Fatal)
		}
		const FToleranceEntry& Entry = Found->second;
		if (OutReason != nullptr)
		{
			*OutReason = Best != nullptr ? Best->Reason : Entry.Reason;
		}
		if (bOutPostRun != nullptr)
		{
			*bOutPostRun = Best != nullptr && Best->bPostRun;
		}
		if (OutSignOff != nullptr)
		{
			*OutSignOff = Best != nullptr ? Best->SignOff : std::string();
		}
		return Best != nullptr ? Best->Value : Entry.Default;
	}

	std::vector<std::string> FTolerances::ListPostRunRegistrations() const
	{
		std::vector<std::string> Out;
		for (const auto& [Key, Entry] : Entries)
		{
			for (const FToleranceOverride& Override : Entry.Overrides)
			{
				if (!Override.bPostRun)
				{
					continue;
				}
				char Value[32];
				snprintf(Value, sizeof(Value), "%.9g", Override.Value);
				Out.push_back(Key + " " + Override.Engine + (Override.Variant.empty() ? "" : "." + Override.Variant)
					+ (Override.Profile.empty() ? "" : " / " + Override.Profile) + " = " + Value + " (sign-off: " + Override.SignOff + ")");
			}
		}
		return Out;
	}

	const FReadbackRule* FTolerances::FindReadbackRule(const std::string_view Engine, const std::string_view Variant, const std::string_view Key,
		const std::string_view Scene) const
	{
		const std::string Scopes[3] = { std::string(Engine) + "." + std::string(Variant), std::string(Engine), "*" };
		const std::string SceneKey = Scene.empty() ? std::string() : std::string(Key) + "@" + std::string(Scene);
		for (const std::string& Scope : Scopes)
		{
			const auto ScopeIt = ReadbackRules.find(Scope);
			if (ScopeIt == ReadbackRules.end())
			{
				continue;
			}
			if (!SceneKey.empty())
			{
				const auto SceneRule = ScopeIt->second.find(SceneKey);
				if (SceneRule != ScopeIt->second.end())
				{
					return &SceneRule->second;
				}
			}
			const auto Rule = ScopeIt->second.find(std::string(Key));
			if (Rule != ScopeIt->second.end())
			{
				return &Rule->second;
			}
		}
		return nullptr;
	}

	const FAllowedSkip* FTolerances::FindAllowedSkip(const std::string_view Gate, const std::string_view Engine, const std::string_view Variant,
		const std::string_view Profile) const
	{
		const auto Found = AllowedSkips.find(Gate);
		if (Found == AllowedSkips.end())
		{
			return nullptr;
		}
		int32 BestScore = -1;
		const FAllowedSkip* Best = nullptr;
		for (const FAllowedSkip& Skip : Found->second)
		{
			if (Skip.Engine != Engine || !MatchesOrAny(Skip.Variant, Variant) || !MatchesOrAny(Skip.Profile, Profile))
			{
				continue;
			}
			const int32 Score = (Skip.Variant.empty() ? 0 : 2) + (Skip.Profile.empty() ? 0 : 1);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = &Skip;
			}
		}
		return Best;
	}
}
