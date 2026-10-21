// PhysicsGates.h - the M6 physics gates G1, G1b, G2-G7, G9, G12 (02_Architecture 14.3, 03_WorkPlan M6, A.12). [MVP M6]
// A gate runs its gate scenes (GateScenes.h) on one target - backend x variant x physics profile - through FPhysScene and
// the C ABI, and compares what the engine did with an analytic reference computed with dt_engine (DL23) or with the
// resolved profile (G9). Every threshold, every expected per-engine deviation and every allowed Skip comes from
// Config/Golden/tolerances.json with its reason; a gate never widens a value by itself. 03 M6 asks for registration
// before the run (02 14.3 "미리 등록"); a value found on a run is registered only after its mechanism is verified in the
// engine source and is marked "registered": "post-run" in the file (the report's note says so).
//
//   G1   S00.FreeFall            |z60 - z_ref| <= 2e-3 cm, |v60 - v_ref| <= 1e-2 cm/s against the dt_engine reference
//                                (501.83328 / -980.00005 for dt 1/60, g -980; Null within 1e-9)
//   G1b  S00d.FreeFallDamped     z60 within 1e-3 cm of the reference of the damping formula the backend declares
//                                (effective key body.damping_formula, canonical = Analytic::EDampingFormula)
//   G2   S01 / S02 / S03         dv 100 cm/s and omega = I^-1 L within 1e-4 (relative); mass, inertia tensor (1e-5 relative)
//                                and COM (1e-5 cm) read back; capsules rest at 75 / 25 cm within 0.5 cm
//   G3   S04 (viz)               step-1 contacts: normal . (+Z) > 0.999, Phi < 0, PointOn1 on the ground, PointOn0 on the body
//                                at the step's narrow-phase pose (the start-of-step pose - Jolt, Bullet, PhysX - or the free-
//                                motion predicted pose X0 + (v0 + g dt) dt - Chaos; one convention for the whole scene),
//                                Phi = (PointOn0 - PointOn1) . n, the v1.0 flag rule (docs/Abi.md 9.1 decision 2: at most one
//                                of ACTIVE / ACTIVE_APPROX / SPECULATIVE, SPECULATIVE only with Phi > 0, a penetrating point
//                                ACTIVE_APPROX without exact impulses, ACTIVE on at least one point of each pair with them;
//                                with a normal impulse: impulse > eps implies ACTIVE, ACTIVE implies impulse >= -eps);
//                                UEParity: a command that wakes the sleeping box drops its kept points until the next step
//   G4   S05                     e from the normal velocity before / after the contact step: Average 0.4, Multiply 0, Max 0.8
//                                under the engine's registered restitution velocity model (default +-0.02, and closer to its
//                                model than to the other); the apex ratio against the discrete flight as a secondary check
//   G5   S06                     stop distance 250.947 cm within 1% (dt_engine, discrete); on a 30 deg incline mu 0.6 holds
//                                (no acceleration, displacement and speed bounded), mu 0.55 slides
//   G6   M1.SingleBoxDrop / S08  Normalized: no body asleep in 600 frames; UEParity: the bStartAwake = false body is Sleeping
//                                right after creation and after 60 frames at the same height, the other falls; then a
//                                velocity write without PT_CMDF_AUTO_WAKE ((100, 0, 0), the zero vector, an angular one, and
//                                SET_BODY_STATE where declared) wakes the sleeping body and the next step runs at that
//                                velocity (docs/Abi.md 9.1 decision 1); a nearly zero write ((5e-5, 0, 0) cm/s, (0, 0, 5e-5)
//                                rad/s) wakes it or leaves it asleep as command.nearly_zero_velocity_wakes declares (1 / 0)
//   G7   S07                     UEParity 50 / 62.83 / 62.83 rad/s within 0.01; Normalized released (50 / 80 / 120); the
//                                per-step rotation follows the engine's registered rotation model (Bullet, Null / Chaos)
//   G9   M3.BoxPile 1k           every READBACK key equals its resolved value (profile, variant option, scene capacity or a
//                                registered constant) or is registered as not profile-derived with a reason; an unregistered
//                                READBACK key fails; shape.wrapping = 0
//   G12  all of the above        engine error bits 0 on every frame (Jolt update errors, PhysX error callback, Bullet pool
//                                heap fallbacks), plus M3.BoxPile 1k for 120 frames on its declared capacity hints. Selected
//                                alone (-Gates=G12) it still runs every gate before it and returns only its own report
//
// A gate (or a part of one: G2's S03) that needs what a backend does not declare reports Skip only when tolerances.json
// lists the engine under the gate's "allowed_skip" with a reason (Null: no collision, no contacts); any other Skip is a
// Fail (a Chaos Test DLL without contact capture, a provisional-revision mismatch that hides PT_CAP_CONTACT_*).
// The caller resolves the configuration (PT::Benchmark LoadBenchConfig: PT::Conformance cannot depend on it, DL53) and
// passes it in; the gates copy it where a scene needs another run mode (G3: viz). conformance.json is written by the bench
// (PhysicsTestBench -Conformance, PT::Benchmark), which owns the build facts it records.
#pragma once

#include "ConformanceGate.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace PhysicsTest
{
	struct FResolvedPhysicsConfig;

	namespace Gates
	{
		// conformance.json gate_suite_version (the M10 stamp key, DL20): bump it whenever a gate, a gate scene or the meaning
		// of a tolerance changes.
		// 2: M6 round 0 (G3 flag rule and wake drop, G4 velocity model, G5 hold speed, G6 wake writes, registered skips).
		// 3: M6 round 1 (G6 nearly zero writes against command.nearly_zero_velocity_wakes; -InjectEngineErrorBits reaches G12).
		// 4: M6 round 2 (G3 accepts the narrow phase at the start-of-step or the predicted pose; G3 ties ACTIVE and the normal
		//    impulse one way only, G3.normal_impulse_eps; S05 version 2 with TouchGap; conformance.json post-run registrations).
		inline constexpr int32 GateSuiteVersion = 4;

		// One override of a registered value: the most specific match wins (engine + variant + profile > engine + variant >
		// engine + profile > engine > default). Engine is the registry backend name (Null, NullDll, Jolt, Bullet, PhysX).
		struct FToleranceOverride
		{
			std::string Engine;
			std::string Variant;   // "" = any
			std::string Profile;   // "" = any
			double Value = 0.0;
			std::string Reason;
			bool bPostRun = false;   // "registered": "post-run" - found on a run, registered after its mechanism was verified
			// "sign_off" of a post-run registration (required there, refused on a pre-run one): "pending" until the lead accepts
			// the deviation, then who accepted it and when (e.g. "lead 2026-10-22, M6 commit"). The gate's note repeats it.
			std::string SignOff;
		};

		// A gate (or a part of it) an engine may Skip: tolerances.json gates.<Gate>.allowed_skip. Any other Skip is a Fail.
		struct FAllowedSkip
		{
			std::string Engine;
			std::string Variant;   // "" = any
			std::string Profile;   // "" = any
			std::string Reason;
		};

		struct FToleranceEntry
		{
			double Default = 0.0;
			std::string Unit;
			std::string Reason;
			std::vector<FToleranceOverride> Overrides;
		};

		// One G9 expectation for a READBACK key (tolerances.json gates.G9.readback.<engine or *>.<key>).
		struct FReadbackRule
		{
			std::string Expect;      // "profile:<name>", "option:<VariantKey>", "init:<Key>:<default>", "const:<number>",
			                         // "capacity:bodies|pairs|contacts", "min:<number>", "text_contains_options", "text_flags";
			                         // empty = unchecked
			std::string Fallback;    // profile:restitution_threshold when the profile leaves it to the engine: "1000dt" or
			                         // "option:<Key>"
			std::vector<std::string> Options;                   // text_contains_options: variant keys whose values the native text names
			std::map<std::string, std::string> Flags;           // text_flags: "<name in the text>" -> "option:<Key>" / "profile:<name>"
			double Rel = 1e-6;
			double Abs = 1e-9;
			std::string Unchecked;   // the reason this READBACK key has no resolved counterpart
			std::string Reason;      // why the expectation is what it is
		};

		class FTolerances
		{
		public:
			bool LoadFile(const std::filesystem::path& Path, std::string& OutError);
			bool LoadText(std::string_view Text, const std::string& DisplayName, std::string& OutError);
			// The registered value of Gate.Check for the target (Engine / Variant / Profile); Fatal when the check is not
			// registered (a gate must never run on an unregistered tolerance). OutReason: the matching override's reason, or
			// the default's.
			[[nodiscard]] double Get(std::string_view Gate, std::string_view Check, std::string_view Engine, std::string_view Variant,
				std::string_view Profile, std::string* OutReason = nullptr, bool* bOutPostRun = nullptr, std::string* OutSignOff = nullptr) const;
			// The override Get returns for the target (most specific match), or nullptr when the default applies. Fatal like Get
			// when the check is not registered.
			[[nodiscard]] const FToleranceOverride* FindOverride(std::string_view Gate, std::string_view Check, std::string_view Engine,
				std::string_view Variant, std::string_view Profile) const;
			// Every post-run registration as "<Gate>.<Check> <engine>[.<variant>][ / <profile>] = <value> (sign-off: <sign_off>)", by
			// key: what still waits for the lead's sign-off is visible in one list (ToleranceFileIsComplete prints it).
			[[nodiscard]] std::vector<std::string> ListPostRunRegistrations() const;
			[[nodiscard]] bool Has(std::string_view Gate, std::string_view Check) const;
			// The G9 rule of Key for Engine / Variant: "<Engine>.<Variant>" first, then "<Engine>", then "*". nullptr = none.
			[[nodiscard]] const FReadbackRule* FindReadbackRule(std::string_view Engine, std::string_view Variant, std::string_view Key) const;
			// The registered Skip of Gate for the target (most specific match), or nullptr: an unregistered Skip fails the gate.
			[[nodiscard]] const FAllowedSkip* FindAllowedSkip(std::string_view Gate, std::string_view Engine, std::string_view Variant,
				std::string_view Profile) const;
			[[nodiscard]] const std::string& GetSha256() const { return Sha256; }
			[[nodiscard]] const std::string& GetSourceName() const { return SourceName; }
			[[nodiscard]] int32 GetVersion() const { return Version; }

		private:
			std::map<std::string, FToleranceEntry, std::less<>> Entries;                                         // "G1.z60_abs_cm"
			std::map<std::string, std::map<std::string, FReadbackRule, std::less<>>, std::less<>> ReadbackRules;   // scope -> key -> rule
			std::map<std::string, std::vector<FAllowedSkip>, std::less<>> AllowedSkips;                          // gate -> skips
			std::string Sha256;
			std::string SourceName;
			int32 Version = 0;
		};

		// <repo>/Config/Golden/tolerances.json under ConfigDir.
		[[nodiscard]] std::filesystem::path GetTolerancesPath(const std::filesystem::path& ConfigDir);

		struct FGateTarget
		{
			std::string Backend;                              // registry name
			std::string Variant;                              // resolved variant name ("" = none)
			std::string Profile;                              // resolved physics profile name
			const FResolvedPhysicsConfig* Config = nullptr;   // required
			const FTolerances* Tolerances = nullptr;          // required
			// Test only (PhysicsTestBench -Conformance -InjectEngineErrorBits): OR-ed into every gate frame's engine error bits,
			// so G12 can be shown to fail (PhysicsTest.Bench.ConformanceG12Injected). 0 in every real run.
			uint32 InjectedEngineErrorBits = 0;
			[[nodiscard]] std::string GetEngineLabel() const;   // "PhysX.UE4Compat", "Jolt.Default", "Null"
			[[nodiscard]] std::string GetLabel() const;         // "PhysX.UE4Compat / UEParity"
		};

		// What the gates of one target share: the engine error bits of every frame they ran (G12) and the scenes they ran.
		struct FGateRunState
		{
			uint32 AccumulatedEngineErrorBits = 0;
			int64 NumFramesRun = 0;
			std::vector<std::string> ScenesRun;               // "S00.FreeFall (60 frames)"
			std::vector<std::string> ErrorFrames;             // "<scene> frame <n>: 0x.." (first few)
		};

		class IConformanceGate
		{
		public:
			virtual ~IConformanceGate() = default;
			[[nodiscard]] virtual const char* GetId() const = 0;             // "G1", "G1b", ...
			[[nodiscard]] virtual const char* GetDescription() const = 0;
			virtual FGateReport Run(const FGateTarget& Target, FGateRunState& State) = 0;
		};

		// G1, G1b, G2, G3, G4, G5, G6, G7, G9, G12 in that order (G12 last: it reads the other gates' frames).
		[[nodiscard]] const std::vector<IConformanceGate*>& GetPhysicsGates();
		[[nodiscard]] IConformanceGate* FindPhysicsGate(std::string_view Id);   // case-insensitive

		// Runs the named gates (empty = all) on the target and returns their reports in suite order. G12 judges the frames of
		// every gate before it: when it is selected, those gates run as well and only the selected reports are returned. A
		// Skip that tolerances.json does not allow becomes a Fail. A GLOBAL_SCENE_SETTINGS module that is idle and initialised
		// with other init options is terminated first (PhysX: one tolerances scale per process; the automation process runs
		// SdkDefault after the UE4Compat family). False + OutError when the target cannot run at all (unknown gate, backend
		// unavailable, module busy with other init options).
		bool RunPhysicsGates(const FGateTarget& Target, const std::vector<std::string>& GateIds, std::vector<FGateReport>& OutReports,
			std::string& OutError);
	}
}
