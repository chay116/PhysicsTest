// EffectiveSettings.cpp - see Physics/EffectiveSettings.h.
#include "Physics/EffectiveSettings.h"

#include "Physics/PhysicsBackend.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <nlohmann/json.hpp>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <set>

namespace PhysicsTest
{
	namespace
	{
		const char* const GGenericKeys[] = {
			"world.gravity",
			"solver.type",
			"solver.iterations",
			"broadphase.type",
			"contact.restitution_threshold",
			"contact.offset_rule",
			"contact.speculative_distance",
			"material.friction_combine",
			"material.restitution_combine",
			"material.static_friction_rule",
			"body.damping_formula",
			"body.damping",
			"body.max_angular_velocity",
			"body.max_linear_velocity",
			"body.gyroscopic",
			"sleep.enabled",
			"sleep.criteria",
			"ccd.enabled",
			"threads.workers",
			"determinism.flag",
			"shape.box_margin",
			"shape.convex_radius",
			"shape.wrapping",
			"listeners.installed",
		};

		bool IsKnownSource(const std::string& Source)
		{
			return Source == "READBACK" || Source == "SET_ONLY" || Source == "COMPILE_TIME" || Source == "ENGINE_DEFAULT";
		}
	}

	bool ParseEffectiveSettings(const std::string_view JsonLines, std::vector<FEffectiveSetting>& Out, std::string& OutError)
	{
		Out.clear();
		std::set<std::string, std::less<>> Seen;
		size_t Begin = 0;
		int32 LineNumber = 0;
		while (Begin < JsonLines.size())
		{
			size_t End = JsonLines.find('\n', Begin);
			if (End == std::string_view::npos)
			{
				End = JsonLines.size();
			}
			const std::string_view Line = JsonLines.substr(Begin, End - Begin);
			Begin = End + 1;
			++LineNumber;
			if (Line.find_first_not_of(" \t\r") == std::string_view::npos)
			{
				continue;
			}
			const nlohmann::json Object = nlohmann::json::parse(Line, nullptr, false);
			const auto Fail = [&OutError, LineNumber, Line](const std::string& Why)
			{
				OutError = "effective settings line " + std::to_string(LineNumber) + ": " + Why + " (" + std::string(Line) + ")";
				return false;
			};
			if (Object.is_discarded() || !Object.is_object())
			{
				return Fail("not a JSON object");
			}
			for (const char* Field : { "key", "native", "native_unit", "unit", "source" })
			{
				if (!Object.contains(Field) || !Object[Field].is_string())
				{
					return Fail(std::string("field '") + Field + "' is missing or not a string");
				}
			}
			if (!Object.contains("canonical") || !(Object["canonical"].is_number() || Object["canonical"].is_null()))
			{
				return Fail("field 'canonical' is missing or neither a number nor null");
			}
			FEffectiveSetting Setting;
			Setting.Key = Object["key"].get<std::string>();
			Setting.Native = Object["native"].get<std::string>();
			Setting.NativeUnit = Object["native_unit"].get<std::string>();
			Setting.Unit = Object["unit"].get<std::string>();
			Setting.Source = Object["source"].get<std::string>();
			Setting.bHasCanonical = Object["canonical"].is_number();
			Setting.Canonical = Setting.bHasCanonical ? Object["canonical"].get<double>() : 0.0;
			if (Setting.Key.empty())
			{
				return Fail("empty key");
			}
			if (!IsKnownSource(Setting.Source))
			{
				return Fail("unknown source '" + Setting.Source + "'");
			}
			if (!Seen.insert(Setting.Key).second)
			{
				return Fail("key '" + Setting.Key + "' appears twice");
			}
			Out.push_back(std::move(Setting));
		}
		return true;
	}

	std::span<const char* const> GetGenericEffectiveKeys()
	{
		return GGenericKeys;
	}

	std::vector<std::string> GetRequiredEffectiveKeys(const FBackendInfo& Info)
	{
		std::vector<std::string> Keys(std::begin(GGenericKeys), std::end(GGenericKeys));
		Keys.insert(Keys.end(), Info.RequiredEffectiveKeys.begin(), Info.RequiredEffectiveKeys.end());
		return Keys;
	}

	std::vector<std::string> FindMissingEffectiveKeys(const std::vector<FEffectiveSetting>& Settings, const FBackendInfo& Info)
	{
		std::vector<std::string> Missing;
		for (const std::string& Key : GetRequiredEffectiveKeys(Info))
		{
			const bool bFound = std::any_of(Settings.begin(), Settings.end(), [&Key](const FEffectiveSetting& Setting) { return Setting.Key == Key; });
			if (!bFound)
			{
				Missing.push_back(Key);
			}
		}
		return Missing;
	}
}
