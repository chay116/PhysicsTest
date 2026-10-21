// EffectiveSettings.h - parsing a backend's DumpEffectiveSettings output and the required-key sets (02_Architecture 10
// "Effective readback", DL19, A.10). [MVP M2: Null (gate A7); M3: effective_settings.jsonl; M6: G9 READBACK checks]
// One JSON object per line: {"key", "native", "native_unit", "canonical" (number or null), "unit", "source"}, source one
// of READBACK / SET_ONLY / COMPILE_TIME / ENGINE_DEFAULT (PtBackendKit.h FTextWriter::AppendEffective).
// A.10 sketched these in PhysicsProfile.h; they live in their own header because they describe backend output, not
// profile input.
#pragma once

#include "CoreTypes.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace PhysicsTest
{
	struct FBackendInfo;

	struct FEffectiveSetting
	{
		std::string Key;
		std::string Native;
		std::string NativeUnit;
		std::string Unit;
		std::string Source;
		double Canonical = 0.0;
		bool bHasCanonical = false;   // false: "canonical" was null (n/a)
	};

	// Parses JSON lines. Returns false (OutError names the line) for a line that is not an object with the six fields,
	// an unknown source, or a repeated key.
	bool ParseEffectiveSettings(std::string_view JsonLines, std::vector<FEffectiveSetting>& Out, std::string& OutError);

	// The generic set every backend reports (a concept a backend lacks is native "n/a", source COMPILE_TIME):
	//   world.gravity, solver.type, solver.iterations, broadphase.type, contact.restitution_threshold, contact.offset_rule,
	//   contact.speculative_distance, material.friction_combine, material.restitution_combine, material.static_friction_rule,
	//   body.damping_formula, body.damping, body.max_angular_velocity, body.max_linear_velocity, body.gyroscopic,
	//   sleep.enabled, sleep.criteria, ccd.enabled, threads.workers, determinism.flag, shape.box_margin, shape.convex_radius,
	//   shape.wrapping, listeners.installed
	[[nodiscard]] std::span<const char* const> GetGenericEffectiveKeys();
	// The generic set plus the backend's own set (FBackendInfo::RequiredEffectiveKeys, from GetInfo).
	[[nodiscard]] std::vector<std::string> GetRequiredEffectiveKeys(const FBackendInfo& Info);
	// Required keys missing from Settings (empty = gate A7 passes).
	[[nodiscard]] std::vector<std::string> FindMissingEffectiveKeys(const std::vector<FEffectiveSetting>& Settings, const FBackendInfo& Info);
}
