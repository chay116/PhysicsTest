// PhysicsTestScenario.h - scenarios and their static registry (02_Architecture 4.5, A.12, docs/Scenes.md). [MVP M3:
// S00.FreeFall, M1.SingleBoxDrop, M3.BoxPile; gate scenes M6; performance / behaviour scenes M7 / M9]
// A scenario authors one scene through the FPhysScene API only (materials, body setups, SpawnBody, FlushDeferredActors),
// so every backend receives the same marshalled bytes (desc_sha256 is equal across engines, DL43). Scenarios register
// themselves with IMPLEMENT_PHYSICS_TEST_SCENARIO; programs link PT::Scenarios with WHOLE_ARCHIVE (the registrars are
// otherwise unreferenced) and check at start-up that the registry is not empty (FScenarioRegistry::CheckNotEmpty).
#pragma once

#include "CoreTypes.h"
#include "Math/RandomStream.h"

#include <map>
#include <string>
#include <vector>

namespace PhysicsTest
{
	class FPhysScene;
	struct FBodyInstance;
	struct FPhysicalMaterial;
	struct FResolvedPhysicsConfig;

	enum class EScenarioKind : uint8
	{
		Gate,
		Overhead,
		Behavior,
		Perf,
		Extended,
	};

	[[nodiscard]] const char* ToString(EScenarioKind Kind);

	// A frame window the scenario declares for its statistics (02 9.5), inclusive frame indices.
	struct FStepWindow
	{
		std::string Name;
		uint32 First = 0;
		uint32 Last = 0;
	};

	// Parameter values by name (-Param:Name=Value). Resolved maps hold every declared parameter.
	using FScenarioParams = std::map<std::string, std::string, std::less<>>;

	// How ResolveParams checks and normalises a parameter value (M3 fix round: a seed of "abc" must not become 0).
	enum class EScenarioParamType : uint8
	{
		Text,    // any value, or one of AllowedValues
		Int32,   // a decimal integer in [-2^31, 2^31 - 1]: optional '-', digits only; stored canonically ("024301" -> "24301")
	};

	[[nodiscard]] const char* ToString(EScenarioParamType Type);

	struct FScenarioParamDesc
	{
		std::string Name;
		std::string Default;
		std::vector<std::string> AllowedValues;   // Text: empty = any value; Int32: unused
		std::string Description;
		EScenarioParamType Type = EScenarioParamType::Text;
	};

	// The scene's declared counts: FPhysSceneInitParams' capacity hints (02 9.4), set before the scene exists.
	struct FScenarioCapacity
	{
		uint32 MaxBodies = 0;
		uint32 MaxBodyPairs = 0;
		uint32 MaxContactConstraints = 0;
	};

	// What BuildScene gets besides the scene.
	struct FScenarioContext
	{
		const FScenarioParams& Params;               // resolved: every declared parameter
		FRandomStream& Random;                       // seeded from the Seed parameter (or the scenario default)
		const FResolvedPhysicsConfig* Config = nullptr;   // nullptr = built-in defaults

		// FBodyInstance / FPhysicalMaterial defaults with the profile's [BodyInstanceDefaults] / [PhysicalMaterialDefaults]
		// applied: what a scenario starts from BEFORE authoring (02 10).
		[[nodiscard]] FBodyInstance MakeBodyTemplate() const;
		[[nodiscard]] FPhysicalMaterial MakeMaterialTemplate() const;
		[[nodiscard]] const std::string& GetParam(const char* Name) const;
	};

	class IPhysicsTestScenario
	{
	public:
		virtual ~IPhysicsTestScenario() = default;
		// Catalog ID.Name (docs/Scenes.md), e.g. "M3.BoxPile".
		[[nodiscard]] virtual const char* GetName() const = 0;
		[[nodiscard]] virtual int32 GetVersion() const = 0;
		[[nodiscard]] virtual EScenarioKind GetKind() const = 0;
		[[nodiscard]] virtual bool IsChaotic() const = 0;
		[[nodiscard]] virtual const char* GetDescription() const = 0;
		[[nodiscard]] virtual std::vector<FScenarioParamDesc> GetParamDescs() const { return {}; }
		[[nodiscard]] virtual int32 GetDefaultFrames(const FScenarioParams& Params) const = 0;
		[[nodiscard]] virtual std::vector<FStepWindow> GetWindows(const FScenarioParams& Params) const = 0;
		[[nodiscard]] virtual FScenarioCapacity GetCapacity(const FScenarioParams& Params) const = 0;
		// Builds the scene (ground = static box, body index 0, where the scenario has a ground) and flushes it.
		virtual void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) = 0;
		virtual void OnPreTick(FPhysScene& /*Scene*/, float /*DeltaSeconds*/) {}
		[[nodiscard]] virtual uint64 GetRequiredCapabilities() const { return 0; }
		// The random seed when no Seed parameter is given (FRandomStream::DefaultSeed, 0x5EED).
		[[nodiscard]] virtual int32 GetDefaultSeed() const { return FRandomStream::DefaultSeed; }

		// Every declared parameter with its default, then Overrides on top. False + OutError for an undeclared name, a value
		// outside AllowedValues or a value its Type rejects. Int32 values are stored canonically, so scene_sha256 hashes the
		// value, not its spelling.
		bool ResolveParams(const FScenarioParams& Overrides, FScenarioParams& OutResolved, std::string& OutError) const;
		// The FRandomStream seed of resolved parameters: the Seed parameter (declared Int32, validated by ResolveParams)
		// or GetDefaultSeed() when the scenario declares none. The one place every caller (bench, tests) takes it from.
		[[nodiscard]] int32 GetSeed(const FScenarioParams& Resolved) const;
		// scene_sha256 (02 12.3): SHA-256 of the canonical JSON {"name","version","params"} of resolved parameters.
		[[nodiscard]] std::string ComputeSceneSha256(const FScenarioParams& Resolved) const;
		[[nodiscard]] std::string MakeCanonicalJson(const FScenarioParams& Resolved) const;
	};

	class FScenarioRegistry
	{
	public:
		[[nodiscard]] static FScenarioRegistry& Get();
		void Register(IPhysicsTestScenario* Scenario);
		[[nodiscard]] IPhysicsTestScenario* Find(const std::string& Name) const;   // case-insensitive
		[[nodiscard]] std::vector<IPhysicsTestScenario*> GetAll() const;           // sorted by name
		// Fatal when no scenario registered (a program linked PT::Scenarios without WHOLE_ARCHIVE, A.12).
		static void CheckNotEmpty(const char* Program);

	private:
		std::vector<IPhysicsTestScenario*> Scenarios;
	};

	// One static instance per scenario class; the registry keeps a pointer to it.
	template <class TScenario>
	struct TScenarioRegistrar
	{
		TScenario Instance;
		TScenarioRegistrar() { FScenarioRegistry::Get().Register(&Instance); }
	};
}

// Static registration (A.12). PrettyName is a unique C++ identifier for the registrar object.
#define IMPLEMENT_PHYSICS_TEST_SCENARIO(Class, PrettyName) \
	namespace { ::PhysicsTest::TScenarioRegistrar<Class> GScenarioRegistrar_##PrettyName; }
