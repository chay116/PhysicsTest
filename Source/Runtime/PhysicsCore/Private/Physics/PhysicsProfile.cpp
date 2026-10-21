// PhysicsProfile.cpp - profile / variant files and FProfileResolver (02_Architecture 10, DL18, DL44).
// Values are parsed and re-exported through per-struct property tables, so every modelled key is validated, carries an
// Origin, and hashes formatting-independently. UE text formats are accepted: True / False, enum names (optionally
// "EType::Name"), struct text "(Key=Value,...)" for FVector / SolverOptions / ChaosSettings (unknown struct fields are
// ignored with a warning, as UE's ImportText would skip them).
#include "Physics/PhysicsProfile.h"

#include "Containers/StringConv.h"
#include "HAL/IConsoleManager.h"
#include "Hash/Sha256.h"
#include "Misc/Parse.h"
#include "Physics/PhysicsConsoleVariables.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <span>
#include <type_traits>

namespace PhysicsTest
{
	namespace
	{
		// ------------------------------------------------------------------------------------------------------------
		// Text helpers
		// ------------------------------------------------------------------------------------------------------------
		char ToLowerAscii(const char C)
		{
			return (C >= 'A' && C <= 'Z') ? static_cast<char>(C - 'A' + 'a') : C;
		}

		std::string ToLower(const std::string_view Text)
		{
			std::string Result(Text);
			for (char& C : Result)
			{
				C = ToLowerAscii(C);
			}
			return Result;
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

		bool StartsWithNoCase(const std::string_view Text, const std::string_view Prefix)
		{
			return Text.size() >= Prefix.size() && EqualsNoCase(Text.substr(0, Prefix.size()), Prefix);
		}

		std::string_view Trim(std::string_view Text)
		{
			while (!Text.empty() && (Text.front() == ' ' || Text.front() == '\t' || Text.front() == '\r' || Text.front() == '\n'))
			{
				Text.remove_prefix(1);
			}
			while (!Text.empty() && (Text.back() == ' ' || Text.back() == '\t' || Text.back() == '\r' || Text.back() == '\n'))
			{
				Text.remove_suffix(1);
			}
			return Text;
		}

		std::string Unquote(std::string_view Text)
		{
			Text = Trim(Text);
			if (Text.size() >= 2 && Text.front() == '"' && Text.back() == '"')
			{
				Text = Text.substr(1, Text.size() - 2);
			}
			return std::string(Text);
		}

		template <typename T>
		std::string FormatShortest(const T Value)
		{
			char Buffer[64];
			const auto [Ptr, Error] = std::to_chars(Buffer, Buffer + sizeof(Buffer), Value);
			return Error == std::errc() ? std::string(Buffer, Ptr) : std::string("0");
		}

		template <typename T>
		bool ParseNumber(std::string_view Text, T& OutValue)
		{
			Text = Trim(Text);
			if (!Text.empty() && Text.front() == '+')
			{
				Text.remove_prefix(1);
			}
			int Base = 10;
			if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>)
			{
				if (Text.size() > 2 && Text[0] == '0' && (Text[1] == 'x' || Text[1] == 'X'))
				{
					Text.remove_prefix(2);
					Base = 16;
				}
			}
			if (Text.empty())
			{
				return false;
			}
			T Value{};
			std::from_chars_result Result{};
			if constexpr (std::is_integral_v<T>)
			{
				Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value, Base);
			}
			else
			{
				Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
			}
			if (Result.ec != std::errc() || Result.ptr != Text.data() + Text.size())
			{
				return false;
			}
			OutValue = Value;
			return true;
		}

		// "(A=1,B=(X=1,Y=2),C="text")" -> {A,1},{B,(X=1,Y=2)},{C,text}. Commas inside nested parentheses or quotes
		// do not split.
		bool ParseStructText(std::string_view Text, std::vector<std::pair<std::string, std::string>>& OutPairs, std::string& OutError)
		{
			OutPairs.clear();
			Text = Trim(Text);
			if (Text.size() < 2 || Text.front() != '(' || Text.back() != ')')
			{
				OutError = "expected struct text '(Key=Value,...)', got '" + std::string(Text) + "'";
				return false;
			}
			const std::string_view Inner = Text.substr(1, Text.size() - 2);
			int32 Depth = 0;
			bool bInQuote = false;
			size_t Start = 0;
			for (size_t Index = 0; Index <= Inner.size(); ++Index)
			{
				const bool bEnd = Index == Inner.size();
				const char C = bEnd ? ',' : Inner[Index];
				if (!bEnd && C == '"')
				{
					bInQuote = !bInQuote;
				}
				else if (!bInQuote && C == '(')
				{
					++Depth;
				}
				else if (!bInQuote && C == ')')
				{
					--Depth;
				}
				else if ((bEnd || (!bInQuote && Depth == 0)) && C == ',')
				{
					const std::string_view Item = Trim(Inner.substr(Start, Index - Start));
					Start = Index + 1;
					if (Item.empty())
					{
						continue;
					}
					const size_t Equals = Item.find('=');
					if (Equals == std::string_view::npos)
					{
						OutError = "expected Key=Value inside struct text, got '" + std::string(Item) + "'";
						return false;
					}
					OutPairs.emplace_back(std::string(Trim(Item.substr(0, Equals))), Unquote(Item.substr(Equals + 1)));
				}
			}
			if (Depth != 0 || bInQuote)
			{
				OutError = "unbalanced parentheses or quotes in struct text '" + std::string(Text) + "'";
				return false;
			}
			return true;
		}

		// Unknown struct fields are reported here while a resolve runs (UE's ImportText skips them with a warning).
		thread_local std::vector<std::string>* GImportWarnings = nullptr;

		// ------------------------------------------------------------------------------------------------------------
		// Value codecs
		// ------------------------------------------------------------------------------------------------------------
		template <typename T>
		struct TEnumNames;

#define PT_ENUM_NAMES(EnumType, ...) \
		template <> \
		struct TEnumNames<EnumType> \
		{ \
			static constexpr const char* TypeName = #EnumType; \
			static constexpr std::array Values = { __VA_ARGS__ }; \
		}

		PT_ENUM_NAMES(EFrictionCombineMode, EFrictionCombineMode::Average, EFrictionCombineMode::Min, EFrictionCombineMode::Multiply, EFrictionCombineMode::Max);
		PT_ENUM_NAMES(ESleepFamily, ESleepFamily::Normal, ESleepFamily::Sensitive, ESleepFamily::Custom);
		PT_ENUM_NAMES(EChaosThreadingMode, EChaosThreadingMode::DedicatedThread, EChaosThreadingMode::TaskGraph, EChaosThreadingMode::SingleThread);
		PT_ENUM_NAMES(EPhysicsTickMode, EPhysicsTickMode::FixedStep, EPhysicsTickMode::Sync, EPhysicsTickMode::Substepping, EPhysicsTickMode::AsyncFixed);
		PT_ENUM_NAMES(ESubstepClampRule, ESubstepClampRule::UE5ChaosScene, ESubstepClampRule::UE4PhysSubstep);
		PT_ENUM_NAMES(EFrameDriver, EFrameDriver::Auto, EFrameDriver::Harness, EFrameDriver::Native);
		PT_ENUM_NAMES(EPhysicsRunMode, EPhysicsRunMode::Bench, EPhysicsRunMode::Behavior, EPhysicsRunMode::Viz);
		PT_ENUM_NAMES(EMassRule, EMassRule::UEBodyUtils, EMassRule::Physical);
#undef PT_ENUM_NAMES

		bool ImportText(const std::string_view Text, bool& Out, std::string& OutError)
		{
			if (!ConsoleVariablePrivate::ParseBool(Text, Out))
			{
				OutError = "expected True / False, got '" + std::string(Text) + "'";
				return false;
			}
			return true;
		}

		template <typename T>
			requires (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
		bool ImportText(const std::string_view Text, T& Out, std::string& OutError)
		{
			if constexpr (std::is_integral_v<T>)
			{
				using TWide = std::conditional_t<std::is_signed_v<T>, long long, unsigned long long>;
				TWide Wide = 0;
				if (Trim(Text).starts_with('-') && std::is_unsigned_v<T>)
				{
					OutError = "expected a non-negative integer, got '" + std::string(Text) + "'";
					return false;
				}
				if (!ParseNumber(Text, Wide) || Wide < static_cast<TWide>(std::numeric_limits<T>::min())
					|| Wide > static_cast<TWide>(std::numeric_limits<T>::max()))
				{
					OutError = "expected an integer in range, got '" + std::string(Text) + "'";
					return false;
				}
				Out = static_cast<T>(Wide);
				return true;
			}
			else
			{
				// std::from_chars also accepts "inf", "infinity" and "nan": a profile value must be a finite number.
				T Value{};
				if (!ParseNumber(Text, Value) || !std::isfinite(Value))
				{
					OutError = "expected a finite number, got '" + std::string(Text) + "'";
					return false;
				}
				Out = Value;
				return true;
			}
		}

		template <typename T>
			requires std::is_enum_v<T>
		bool ImportText(std::string_view Text, T& Out, std::string& OutError)
		{
			Text = Trim(Text);
			const size_t Scope = Text.rfind("::");
			if (Scope != std::string_view::npos)
			{
				Text = Text.substr(Scope + 2);   // "EFrictionCombineMode::Average"
			}
			for (const T Value : TEnumNames<T>::Values)
			{
				if (EqualsNoCase(Text, ToString(Value)))
				{
					Out = Value;
					return true;
				}
			}
			OutError = std::string("unknown ") + TEnumNames<T>::TypeName + " value '" + std::string(Text) + "'";
			return false;
		}

		std::string ExportText(const bool Value)
		{
			return Value ? "True" : "False";
		}

		template <typename T>
			requires (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
		std::string ExportText(const T Value)
		{
			if constexpr (std::is_same_v<T, uint64>)
			{
				char Buffer[32];
				const auto [Ptr, Error] = std::to_chars(Buffer, Buffer + sizeof(Buffer), Value, 16);
				return Error == std::errc() ? "0x" + std::string(Buffer, Ptr) : std::string("0x0");
			}
			else if constexpr (std::is_integral_v<T>)
			{
				return std::to_string(Value);
			}
			else
			{
				return FormatShortest(Value);
			}
		}

		template <typename T>
			requires std::is_enum_v<T>
		std::string ExportText(const T Value)
		{
			return ToString(Value);
		}

		// Struct codecs, defined after the field tables below (declared here so the templates can see them).
		bool ImportText(std::string_view Text, FChaosSolverConfiguration& Out, std::string& OutError);
		bool ImportText(std::string_view Text, FChaosPhysicsSettings& Out, std::string& OutError);
		bool ImportText(std::string_view Text, FVector& Out, std::string& OutError);
		std::string ExportText(const FChaosSolverConfiguration& Value);
		std::string ExportText(const FChaosPhysicsSettings& Value);
		std::string ExportText(const FVector& Value);

		// ---- property tables ----
		struct FPropertyDesc
		{
			const char* Name;
			const char* Unit;
			bool bPhysics;   // part of physics_sha256 (false: run options)
			bool (*Import)(void* Object, std::string_view Text, std::string& OutError);
			std::string (*Export)(const void* Object);
			void (*Copy)(void* Destination, const void* Source);
		};

		template <typename TObject, auto Member>
		struct TPropertyOps
		{
			using TField = std::remove_cvref_t<decltype(std::declval<TObject&>().*Member)>;

			static bool Import(void* Object, const std::string_view Text, std::string& OutError)
			{
				TField Value{};
				if (!ImportText(Text, Value, OutError))
				{
					return false;
				}
				static_cast<TObject*>(Object)->*Member = Value;
				return true;
			}
			static std::string Export(const void* Object)
			{
				return ExportText(static_cast<const TObject*>(Object)->*Member);
			}
			static void Copy(void* Destination, const void* Source)
			{
				static_cast<TObject*>(Destination)->*Member = static_cast<const TObject*>(Source)->*Member;
			}
		};

#define PT_PROPERTY(Object, Field, Unit, bPhysics) \
		FPropertyDesc{ #Field, Unit, bPhysics, &TPropertyOps<Object, &Object::Field>::Import, &TPropertyOps<Object, &Object::Field>::Export, \
			&TPropertyOps<Object, &Object::Field>::Copy }

		// Struct values: start from the struct default (UE ImportText of a config string onto a fresh CDO value), apply
		// the listed fields, report unknown ones.
		template <typename TStruct>
		bool ImportStructText(const std::string_view Text, TStruct& Out, std::span<const FPropertyDesc> Fields, std::string& OutError)
		{
			std::vector<std::pair<std::string, std::string>> Pairs;
			if (!ParseStructText(Text, Pairs, OutError))
			{
				return false;
			}
			TStruct Value{};
			for (const auto& [Key, FieldText] : Pairs)
			{
				const auto Field = std::find_if(Fields.begin(), Fields.end(), [&Key](const FPropertyDesc& Desc) { return EqualsNoCase(Desc.Name, Key); });
				if (Field == Fields.end())
				{
					if (GImportWarnings != nullptr)
					{
						GImportWarnings->push_back("struct field '" + Key + "' is not modelled by the harness and is ignored");
					}
					continue;
				}
				if (!Field->Import(&Value, FieldText, OutError))
				{
					OutError = std::string(Field->Name) + ": " + OutError;
					return false;
				}
			}
			Out = Value;
			return true;
		}

		template <typename TStruct>
		std::string ExportStructText(const TStruct& Value, std::span<const FPropertyDesc> Fields)
		{
			std::string Text = "(";
			for (const FPropertyDesc& Field : Fields)
			{
				if (Text.size() > 1)
				{
					Text += ',';
				}
				Text += Field.Name;
				Text += '=';
				Text += Field.Export(&Value);
			}
			Text += ')';
			return Text;
		}

		const FPropertyDesc GSolverOptionsFields[] = {
			PT_PROPERTY(FChaosSolverConfiguration, PositionIterations, "", true),
			PT_PROPERTY(FChaosSolverConfiguration, VelocityIterations, "", true),
			PT_PROPERTY(FChaosSolverConfiguration, ProjectionIterations, "", true),
			PT_PROPERTY(FChaosSolverConfiguration, CollisionMarginFraction, "", true),
			PT_PROPERTY(FChaosSolverConfiguration, CollisionMarginMax, "cm", true),
			PT_PROPERTY(FChaosSolverConfiguration, CollisionCullDistance, "cm", true),
			PT_PROPERTY(FChaosSolverConfiguration, CollisionMaxPushOutVelocity, "cm/s", true),
			PT_PROPERTY(FChaosSolverConfiguration, CollisionInitialOverlapDepenetrationVelocity, "cm/s", true),
		};
		const FPropertyDesc GChaosSettingsFields[] = {
			PT_PROPERTY(FChaosPhysicsSettings, DefaultThreadingModel, "", false),
		};
		const FPropertyDesc GVectorFields[] = {
			PT_PROPERTY(FVector, X, "", true),
			PT_PROPERTY(FVector, Y, "", true),
			PT_PROPERTY(FVector, Z, "", true),
		};

		bool ImportText(const std::string_view Text, FChaosSolverConfiguration& Out, std::string& OutError)
		{
			return ImportStructText(Text, Out, GSolverOptionsFields, OutError);
		}
		bool ImportText(const std::string_view Text, FChaosPhysicsSettings& Out, std::string& OutError)
		{
			return ImportStructText(Text, Out, GChaosSettingsFields, OutError);
		}
		bool ImportText(const std::string_view Text, FVector& Out, std::string& OutError)
		{
			return ImportStructText(Text, Out, GVectorFields, OutError);
		}
		std::string ExportText(const FChaosSolverConfiguration& Value)
		{
			return ExportStructText(Value, GSolverOptionsFields);
		}
		std::string ExportText(const FChaosPhysicsSettings& Value)
		{
			return ExportStructText(Value, GChaosSettingsFields);
		}
		std::string ExportText(const FVector& Value)
		{
			return ExportStructText(Value, GVectorFields);
		}

		// [/Script/Engine.PhysicsSettings]: UPhysicsSettingsCore + UPhysicsSettings fields the harness models.
		const FPropertyDesc GPhysicsSettingsProperties[] = {
			PT_PROPERTY(FPhysicsSettings, DefaultGravityZ, "cm/s^2", true),
			PT_PROPERTY(FPhysicsSettings, SimulateScratchMemorySize, "bytes", true),
			PT_PROPERTY(FPhysicsSettings, bEnableEnhancedDeterminism, "", true),
			PT_PROPERTY(FPhysicsSettings, bEnableShapeSharing, "", true),
			PT_PROPERTY(FPhysicsSettings, bEnablePCM, "", true),
			PT_PROPERTY(FPhysicsSettings, bEnableStabilization, "", true),
			PT_PROPERTY(FPhysicsSettings, BounceThresholdVelocity, "cm/s", true),
			PT_PROPERTY(FPhysicsSettings, FrictionCombineMode, "", true),
			PT_PROPERTY(FPhysicsSettings, RestitutionCombineMode, "", true),
			PT_PROPERTY(FPhysicsSettings, MaxAngularVelocity, "deg/s", true),
			PT_PROPERTY(FPhysicsSettings, MaxDepenetrationVelocity, "cm/s", true),
			PT_PROPERTY(FPhysicsSettings, ContactOffsetMultiplier, "", true),
			PT_PROPERTY(FPhysicsSettings, MinContactOffset, "cm", true),
			PT_PROPERTY(FPhysicsSettings, MaxContactOffset, "cm", true),
			PT_PROPERTY(FPhysicsSettings, SolverOptions, "", true),
			PT_PROPERTY(FPhysicsSettings, MinPhysicsDeltaTime, "s", true),
			PT_PROPERTY(FPhysicsSettings, MaxPhysicsDeltaTime, "s", true),
			PT_PROPERTY(FPhysicsSettings, bSubstepping, "", true),
			PT_PROPERTY(FPhysicsSettings, bTickPhysicsAsync, "", true),
			PT_PROPERTY(FPhysicsSettings, AsyncFixedTimeStepSize, "s", true),
			PT_PROPERTY(FPhysicsSettings, MaxSubstepDeltaTime, "s", true),
			PT_PROPERTY(FPhysicsSettings, MaxSubsteps, "", true),
			PT_PROPERTY(FPhysicsSettings, PhysXTreeRebuildRate, "", true),
			PT_PROPERTY(FPhysicsSettings, ChaosSettings, "", false),
		};

		const FPropertyDesc GPhysicsTestSettingsProperties[] = {
			PT_PROPERTY(FPhysicsTestSettings, TickMode, "", true),
			PT_PROPERTY(FPhysicsTestSettings, FixedDeltaTime, "s", true),
			PT_PROPERTY(FPhysicsTestSettings, SubstepClampRule, "", true),
			PT_PROPERTY(FPhysicsTestSettings, FrameDriver, "", false),
			PT_PROPERTY(FPhysicsTestSettings, MassRule, "", true),
			PT_PROPERTY(FPhysicsTestSettings, bAllowSleeping, "", true),
			PT_PROPERTY(FPhysicsTestSettings, RestitutionThresholdVelocity, "cm/s", true),
			PT_PROPERTY(FPhysicsTestSettings, RunMode, "", false),
			PT_PROPERTY(FPhysicsTestSettings, InstrumentFlags, "", false),
			PT_PROPERTY(FPhysicsTestSettings, NumThreads, "", false),
			PT_PROPERTY(FPhysicsTestSettings, AffinityMask, "", false),
			PT_PROPERTY(FPhysicsTestSettings, HashEvery, "frames", false),
			PT_PROPERTY(FPhysicsTestSettings, CountersEvery, "frames", false),
			PT_PROPERTY(FPhysicsTestSettings, CaptureEvery, "frames", false),
		};

		const FPropertyDesc GBodyInstanceDefaultsProperties[] = {
			PT_PROPERTY(FBodyInstance, bEnableGravity, "", true),
			PT_PROPERTY(FBodyInstance, bGyroscopicTorqueEnabled, "", true),
			PT_PROPERTY(FBodyInstance, bStartAwake, "", true),
			PT_PROPERTY(FBodyInstance, MassScale, "", true),
			PT_PROPERTY(FBodyInstance, LinearDamping, "1/s", true),
			PT_PROPERTY(FBodyInstance, AngularDamping, "1/s", true),
			PT_PROPERTY(FBodyInstance, bOverrideMaxAngularVelocity, "", true),
			PT_PROPERTY(FBodyInstance, MaxAngularVelocity, "deg/s", true),
			PT_PROPERTY(FBodyInstance, bOverrideMaxDepenetrationVelocity, "", true),
			PT_PROPERTY(FBodyInstance, MaxDepenetrationVelocity, "cm/s", true),
			PT_PROPERTY(FBodyInstance, bUseCCD, "", true),
			PT_PROPERTY(FBodyInstance, bInertiaConditioning, "", true),
			PT_PROPERTY(FBodyInstance, SleepFamily, "", true),
			PT_PROPERTY(FBodyInstance, CustomSleepThresholdMultiplier, "", true),
			PT_PROPERTY(FBodyInstance, StabilizationThresholdMultiplier, "", true),
			PT_PROPERTY(FBodyInstance, bOverrideIterationCounts, "", true),
			PT_PROPERTY(FBodyInstance, PositionSolverIterationCount, "", true),
			PT_PROPERTY(FBodyInstance, VelocitySolverIterationCount, "", true),
			PT_PROPERTY(FBodyInstance, ProjectionSolverIterationCount, "", true),
		};

		const FPropertyDesc GMaterialDefaultsProperties[] = {
			PT_PROPERTY(FPhysicalMaterial, Friction, "", true),
			PT_PROPERTY(FPhysicalMaterial, StaticFriction, "", true),
			PT_PROPERTY(FPhysicalMaterial, Restitution, "", true),
			PT_PROPERTY(FPhysicalMaterial, FrictionCombineMode, "", true),
			PT_PROPERTY(FPhysicalMaterial, bOverrideFrictionCombineMode, "", true),
			PT_PROPERTY(FPhysicalMaterial, RestitutionCombineMode, "", true),
			PT_PROPERTY(FPhysicalMaterial, bOverrideRestitutionCombineMode, "", true),
			PT_PROPERTY(FPhysicalMaterial, Density, "g/cm^3", true),
			PT_PROPERTY(FPhysicalMaterial, RaiseMassToPower, "", true),
			PT_PROPERTY(FPhysicalMaterial, SleepLinearVelocityThreshold, "cm/s", true),
			PT_PROPERTY(FPhysicalMaterial, SleepAngularVelocityThreshold, "rad/s", true),
			PT_PROPERTY(FPhysicalMaterial, SleepCounterThreshold, "ticks", true),
		};
#undef PT_PROPERTY

		// ------------------------------------------------------------------------------------------------------------
		// Sections
		// ------------------------------------------------------------------------------------------------------------
		enum class ESectionKind : uint8
		{
			PhysicsSettings,
			PhysicsTestSettings,
			BodyInstanceDefaults,
			PhysicalMaterialDefaults,
			ConsoleVariables,
			Backend,
			BackendInit,
			ProfileMeta,
			VariantMeta,
			PhysicsSettingsCore,
			ForcedOverrides,
			Unknown,
		};

		struct FSectionInfo
		{
			ESectionKind Kind = ESectionKind::Unknown;
			std::string Prefix;   // canonical key prefix
			std::string Engine;   // Backend kinds
		};

		constexpr std::array<const char*, 5> GKnownEngines = { "Null", "Jolt", "Bullet", "PhysX", "Chaos" };

		std::span<const FPropertyDesc> GetTable(const ESectionKind Kind)
		{
			switch (Kind)
			{
			case ESectionKind::PhysicsSettings:          return GPhysicsSettingsProperties;
			case ESectionKind::PhysicsTestSettings:      return GPhysicsTestSettingsProperties;
			case ESectionKind::BodyInstanceDefaults:     return GBodyInstanceDefaultsProperties;
			case ESectionKind::PhysicalMaterialDefaults: return GMaterialDefaultsProperties;
			default:                                     return {};
			}
		}

		bool IsConfigKind(const ESectionKind Kind)
		{
			return Kind == ESectionKind::PhysicsSettings || Kind == ESectionKind::PhysicsTestSettings || Kind == ESectionKind::BodyInstanceDefaults
				|| Kind == ESectionKind::PhysicalMaterialDefaults || Kind == ESectionKind::ConsoleVariables;
		}

		// Section names compare case-insensitively, as UE's FConfigFile (TMap<FString, FConfigSection>,
		// ConfigCacheIni.h:502, :574 [V]); Prefix and Engine are the canonical spellings.
		FSectionInfo ClassifySection(const std::string_view Section)
		{
			FSectionInfo Info;
			const auto Is = [Section](const char* Name) { return EqualsNoCase(Section, Name); };
			if (Is(ConfigSections::PhysicsSettings))
			{
				Info.Kind = ESectionKind::PhysicsSettings;
				Info.Prefix = "PhysicsSettings";
			}
			else if (Is(ConfigSections::PhysicsTestSettings))
			{
				Info.Kind = ESectionKind::PhysicsTestSettings;
				Info.Prefix = "PhysicsTestSettings";
			}
			else if (Is(ConfigSections::BodyInstanceDefaults))
			{
				Info.Kind = ESectionKind::BodyInstanceDefaults;
				Info.Prefix = "BodyInstanceDefaults";
			}
			else if (Is(ConfigSections::PhysicalMaterialDefaults))
			{
				Info.Kind = ESectionKind::PhysicalMaterialDefaults;
				Info.Prefix = "PhysicalMaterialDefaults";
			}
			else if (Is(ConfigSections::ConsoleVariables))
			{
				Info.Kind = ESectionKind::ConsoleVariables;
				Info.Prefix = "ConsoleVariables";
			}
			else if (Is(ConfigSections::ProfileMeta))
			{
				Info.Kind = ESectionKind::ProfileMeta;
			}
			else if (Is(ConfigSections::VariantMeta))
			{
				Info.Kind = ESectionKind::VariantMeta;
			}
			else if (Is(ConfigSections::PhysicsSettingsCore))
			{
				Info.Kind = ESectionKind::PhysicsSettingsCore;
			}
			else if (Is(ConfigSections::ForcedOverrides))
			{
				Info.Kind = ESectionKind::ForcedOverrides;
			}
			else if (StartsWithNoCase(Section, ConfigSections::BackendPrefix))
			{
				std::string_view Rest = Section.substr(std::string_view(ConfigSections::BackendPrefix).size());
				bool bInit = false;
				if (Rest.size() > 5 && EqualsNoCase(Rest.substr(Rest.size() - 5), ".Init"))
				{
					Rest.remove_suffix(5);
					bInit = true;
				}
				for (const char* Engine : GKnownEngines)
				{
					if (EqualsNoCase(Rest, Engine))
					{
						Info.Kind = bInit ? ESectionKind::BackendInit : ESectionKind::Backend;
						Info.Engine = Engine;
						Info.Prefix = std::string("Backend.") + Engine + (bInit ? ".Init" : "");
					}
				}
			}
			return Info;
		}

		const FPropertyDesc* FindProperty(const ESectionKind Kind, const std::string_view Key)
		{
			for (const FPropertyDesc& Desc : GetTable(Kind))
			{
				if (EqualsNoCase(Desc.Name, Key))
				{
					return &Desc;
				}
			}
			return nullptr;
		}

		ESectionKind KindFromPrefix(const std::string_view Prefix)
		{
			if (Prefix == "PhysicsSettings")
			{
				return ESectionKind::PhysicsSettings;
			}
			if (Prefix == "PhysicsTestSettings")
			{
				return ESectionKind::PhysicsTestSettings;
			}
			if (Prefix == "BodyInstanceDefaults")
			{
				return ESectionKind::BodyInstanceDefaults;
			}
			if (Prefix == "PhysicalMaterialDefaults")
			{
				return ESectionKind::PhysicalMaterialDefaults;
			}
			if (Prefix == "ConsoleVariables")
			{
				return ESectionKind::ConsoleVariables;
			}
			return ESectionKind::Unknown;
		}

		// "PhysicsTestSettings.SubstepClampRule", "ConsoleVariables.p.Foo", ...
		bool IsKnownCanonicalKey(const std::string_view Key)
		{
			const size_t Dot = Key.find('.');
			if (Dot == std::string_view::npos || Dot + 1 >= Key.size())
			{
				return false;
			}
			const ESectionKind Kind = KindFromPrefix(Key.substr(0, Dot));
			if (Kind == ESectionKind::ConsoleVariables)
			{
				return true;
			}
			const FPropertyDesc* Desc = FindProperty(Kind, Key.substr(Dot + 1));
			return Desc != nullptr && Key.substr(Dot + 1) == Desc->Name;
		}

		bool ReadTextFile(const std::string& Utf8Path, std::string& OutText, std::string& OutError)
		{
			std::ifstream Stream(StringConv::Utf8ToPath(Utf8Path), std::ios::binary);
			if (!Stream)
			{
				OutError = "cannot open '" + Utf8Path + "'";
				return false;
			}
			OutText.assign(std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>());
			return true;
		}

		// Splits on commas outside double quotes ("a 1, b=\"x,y\"" -> "a 1", "b=\"x,y\"").
		std::vector<std::string> SplitCommands(const std::string_view Text)
		{
			std::vector<std::string> Commands;
			std::string Current;
			bool bInQuote = false;
			for (const char C : Text)
			{
				if (C == '"')
				{
					bInQuote = !bInQuote;
				}
				if (C == ',' && !bInQuote)
				{
					Commands.push_back(Current);
					Current.clear();
					continue;
				}
				Current += C;
			}
			Commands.push_back(Current);
			return Commands;
		}

		// Only the harness physics table counts (Physics/PhysicsConsoleVariables.h), never "whatever this program has
		// registered": the resolved config, the variant policy and physics_sha256 must not depend on the linked objects.
		IConsoleVariable* FindHarnessCVar(const std::string_view Name)
		{
			return PhysicsCVars::FindHarnessConsoleVariable(Name);
		}

		bool ValidateCVarValue(const IConsoleVariable& Variable, const std::string& Value, std::string& OutError)
		{
			bool bOk = true;
			switch (Variable.GetType())
			{
			case EConsoleVariableType::Bool:
			{
				bool Parsed = false;
				bOk = ConsoleVariablePrivate::ParseBool(Value, Parsed);
				break;
			}
			case EConsoleVariableType::Int:
			{
				int32 Parsed = 0;
				bOk = ConsoleVariablePrivate::ParseInt(Value, Parsed);
				break;
			}
			case EConsoleVariableType::Float:
			{
				float Parsed = 0.0f;
				bOk = ConsoleVariablePrivate::ParseFloat(Value, Parsed);
				break;
			}
			case EConsoleVariableType::String:
				break;
			}
			if (!bOk)
			{
				OutError = std::string("value '") + Value + "' does not parse for console variable " + Variable.GetName();
			}
			return bOk;
		}

		// Canonical text of a harness CVar value (bool / int / float re-exported).
		std::string NormalizeCVarValue(const IConsoleVariable& Variable, const std::string& Value)
		{
			switch (Variable.GetType())
			{
			case EConsoleVariableType::Bool:
			{
				bool Parsed = false;
				return ConsoleVariablePrivate::ParseBool(Value, Parsed) ? ConsoleVariablePrivate::ToString(Parsed) : Value;
			}
			case EConsoleVariableType::Int:
			{
				int32 Parsed = 0;
				return ConsoleVariablePrivate::ParseInt(Value, Parsed) ? ConsoleVariablePrivate::ToString(Parsed) : Value;
			}
			case EConsoleVariableType::Float:
			{
				float Parsed = 0.0f;
				return ConsoleVariablePrivate::ParseFloat(Value, Parsed) ? ConsoleVariablePrivate::ToString(Parsed) : Value;
			}
			case EConsoleVariableType::String:
				break;
			}
			return Value;
		}

		void* GetObject(FResolvedPhysicsConfig& Config, const ESectionKind Kind)
		{
			switch (Kind)
			{
			case ESectionKind::PhysicsSettings:          return &Config.Settings;
			case ESectionKind::PhysicsTestSettings:      return &Config.TestSettings;
			case ESectionKind::BodyInstanceDefaults:     return &Config.BodyInstanceDefaults;
			case ESectionKind::PhysicalMaterialDefaults: return &Config.MaterialDefaults;
			default:                                     return nullptr;
			}
		}

		// Builds the CommandLine layer from "-ini:<Name>:[Section]:Key=Value,[Section2]:Key2=Value2" tokens
		// (UE ConfigCacheIni.cpp:2413-2500). Only the PhysicsTest and Engine ini names address the harness config.
		bool ParseIniOverrides(const std::string& CommandLine, FConfigFile& OutFile, std::vector<std::string>& OutWarnings, std::string& OutError)
		{
			const char* Stream = CommandLine.c_str();
			std::string Token;
			int32 Pair = 0;
			while (FParse::Token(Stream, Token))
			{
				if (!StartsWithNoCase(Token, "-ini:"))
				{
					continue;
				}
				const std::string_view Rest = std::string_view(Token).substr(5);
				const size_t NameEnd = Rest.find(':');
				if (NameEnd == std::string_view::npos)
				{
					OutError = "malformed -ini: override '" + Token + "' (expected -ini:PhysicsTest:[Section]:Key=Value)";
					return false;
				}
				const std::string_view IniName = Rest.substr(0, NameEnd);
				if (!EqualsNoCase(IniName, "PhysicsTest") && !EqualsNoCase(IniName, "Engine"))
				{
					OutWarnings.push_back("-ini:" + std::string(IniName) + ": ignored (the harness reads -ini:PhysicsTest: and -ini:Engine:)");
					continue;
				}
				const std::string Settings(Rest.substr(NameEnd + 1));
				const char* SettingsStream = Settings.c_str();
				std::string SettingPair;
				while (FParse::Token(SettingsStream, SettingPair, ','))
				{
					++Pair;
					const size_t Equals = SettingPair.find('=');
					const size_t SectionEnd = Equals == std::string::npos ? std::string::npos : SettingPair.rfind("]:", Equals);
					if (Equals == std::string::npos || SectionEnd == std::string::npos || SectionEnd == 0)
					{
						OutError = "malformed -ini: setting '" + SettingPair + "' (expected [Section]:Key=Value)";
						return false;
					}
					std::string Section = SettingPair.substr(0, SectionEnd);
					std::erase(Section, '[');
					FConfigLine Line;
					Line.Section = Section;
					Line.Key = std::string(Trim(std::string_view(SettingPair).substr(SectionEnd + 2, Equals - SectionEnd - 2)));
					Line.Value = Unquote(std::string_view(SettingPair).substr(Equals + 1));
					Line.LineNumber = Pair;
					OutFile.AddLine(Line);
				}
			}
			return true;
		}

		struct FOriginRecord
		{
			EConfigOrigin Origin = EConfigOrigin::UEDefault;
			std::string Name;
		};

		std::string LineOrigin(const FConfigFile& File, const FConfigLine& Line)
		{
			return File.Name + ":" + std::to_string(Line.LineNumber);
		}
	}

	// --------------------------------------------------------------------------------------------------------------
	// Public API
	// --------------------------------------------------------------------------------------------------------------
	const char* ToString(const EConfigOrigin Origin)
	{
		switch (Origin)
		{
		case EConfigOrigin::UEDefault:      return "UEDefault";
		case EConfigOrigin::PhysicsProfile: return "Profile";
		case EConfigOrigin::BackendVariant: return "Variant";
		case EConfigOrigin::ScenePinned:    return "ScenePinned";
		case EConfigOrigin::CommandLine:    return "CommandLine";
		case EConfigOrigin::ForcedOverride: return "Forced";
		}
		return "?";
	}

	std::string FProfileResolver::MakeCanonicalKey(const std::string_view Section, const std::string_view Key)
	{
		const FSectionInfo Info = ClassifySection(Section);
		if (Info.Prefix.empty())
		{
			return {};
		}
		if (const FPropertyDesc* Desc = FindProperty(Info.Kind, Key))
		{
			return Info.Prefix + "." + Desc->Name;
		}
		if (Info.Kind == ESectionKind::ConsoleVariables)
		{
			if (const IConsoleVariable* Variable = FindHarnessCVar(Key))
			{
				return Info.Prefix + "." + Variable->GetName();
			}
		}
		return Info.Prefix + "." + std::string(Key);
	}

	std::vector<std::pair<std::string, std::string>> FProfileResolver::GetBuiltInDefaults()
	{
		std::vector<std::pair<std::string, std::string>> Result;
		const FPhysicsSettings Settings;
		const FPhysicsTestSettings TestSettings;
		const FBodyInstance Body;
		const FPhysicalMaterial Material;
		const auto Add = [&Result](const char* Prefix, std::span<const FPropertyDesc> Table, const void* Object)
		{
			for (const FPropertyDesc& Desc : Table)
			{
				Result.emplace_back(std::string(Prefix) + "." + Desc.Name, Desc.Export(Object));
			}
		};
		Add("PhysicsSettings", GPhysicsSettingsProperties, &Settings);
		Add("PhysicsTestSettings", GPhysicsTestSettingsProperties, &TestSettings);
		Add("BodyInstanceDefaults", GBodyInstanceDefaultsProperties, &Body);
		Add("PhysicalMaterialDefaults", GMaterialDefaultsProperties, &Material);
		return Result;
	}

	bool FProfileResolver::NormalizeValue(const std::string_view CanonicalKey, const std::string_view Text, std::string& OutCanonical, std::string& OutError)
	{
		const size_t Dot = CanonicalKey.find('.');
		if (Dot == std::string_view::npos)
		{
			OutError = "not a canonical key: " + std::string(CanonicalKey);
			return false;
		}
		const std::string_view Key = CanonicalKey.substr(Dot + 1);
		const ESectionKind Kind = KindFromPrefix(CanonicalKey.substr(0, Dot));
		const FPropertyDesc* Desc = FindProperty(Kind, Key);
		if (Desc == nullptr)
		{
			OutError = "not a modelled key: " + std::string(CanonicalKey);
			return false;
		}
		FResolvedPhysicsConfig Scratch;
		void* Object = GetObject(Scratch, Kind);
		if (!Desc->Import(Object, Text, OutError))
		{
			return false;
		}
		OutCanonical = Desc->Export(Object);
		return true;
	}

	// ---- profile / variant files --------------------------------------------------------------------------------
	bool FPhysicsProfile::FromText(const std::string_view Text, const std::string& DisplayName, FPhysicsProfile& Out, std::string& OutError)
	{
		Out = FPhysicsProfile();
		Out.Ini.Name = DisplayName;
		if (!Out.Ini.ParseText(Text, OutError))
		{
			return false;
		}
		if (Out.Ini.FindSection(ConfigSections::ProfileMeta) == nullptr || !Out.Ini.GetString(ConfigSections::ProfileMeta, "Name", Out.Name) || Out.Name.empty())
		{
			OutError = DisplayName + ": missing [" + std::string(ConfigSections::ProfileMeta) + "] Name=";
			return false;
		}
		std::string VersionText;
		if (Out.Ini.GetString(ConfigSections::ProfileMeta, "Version", VersionText) && !ImportText(VersionText, Out.Version, OutError))
		{
			OutError = DisplayName + ": Version: " + OutError;
			return false;
		}
		std::vector<std::string> EngineDefaultKeys;
		Out.Ini.GetArray(ConfigSections::ProfileMeta, "EngineDefaultKeys", EngineDefaultKeys);
		for (const std::string& Key : EngineDefaultKeys)
		{
			if (!IsKnownCanonicalKey(Key))
			{
				OutError = DisplayName + ": EngineDefaultKeys entry '" + Key + "' is not a known canonical key (e.g. PhysicsTestSettings.SubstepClampRule)";
				return false;
			}
			Out.KeyPolicy[Key] = EProfileKeyPolicy::EngineDefault;
		}
		return true;
	}

	bool FPhysicsProfile::LoadIni(const std::string& Utf8Path, FPhysicsProfile& Out, std::string& OutError)
	{
		std::string Text;
		return ReadTextFile(Utf8Path, Text, OutError) && FromText(Text, Utf8Path, Out, OutError);
	}

	EProfileKeyPolicy FPhysicsProfile::GetKeyPolicy(const std::string_view CanonicalKey) const
	{
		const auto It = KeyPolicy.find(CanonicalKey);
		return It != KeyPolicy.end() ? It->second : EProfileKeyPolicy::Pinned;
	}

	std::string FPhysicsProfile::Sha256() const
	{
		return FSHA256::HashString(Ini.ToCanonicalString());
	}

	bool FBackendVariant::FromText(const std::string_view Text, const std::string& DisplayName, FBackendVariant& Out, std::string& OutError)
	{
		Out = FBackendVariant();
		Out.Ini.Name = DisplayName;
		if (!Out.Ini.ParseText(Text, OutError))
		{
			return false;
		}
		if (!Out.Ini.GetString(ConfigSections::VariantMeta, "Engine", Out.Engine) || !Out.Ini.GetString(ConfigSections::VariantMeta, "Name", Out.Name)
			|| Out.Name.empty())
		{
			OutError = DisplayName + ": missing [" + std::string(ConfigSections::VariantMeta) + "] Engine= / Name=";
			return false;
		}
		if (std::find_if(GKnownEngines.begin(), GKnownEngines.end(), [&Out](const char* Engine) { return Out.Engine == Engine; }) == GKnownEngines.end())
		{
			OutError = DisplayName + ": unknown Engine '" + Out.Engine + "' (Null, Jolt, Bullet, PhysX, Chaos)";
			return false;
		}
		Out.Ini.GetString(ConfigSections::VariantMeta, "Label", Out.Label);
		Out.Ini.GetArray(ConfigSections::VariantMeta, "FidelityNotes", Out.FidelityNotes);
		return true;
	}

	bool FBackendVariant::LoadIni(const std::string& Utf8Path, FBackendVariant& Out, std::string& OutError)
	{
		std::string Text;
		return ReadTextFile(Utf8Path, Text, OutError) && FromText(Text, Utf8Path, Out, OutError);
	}

	std::string FBackendVariant::Sha256() const
	{
		return FSHA256::HashString(Ini.ToCanonicalString());
	}

	// ---- resolved config ----------------------------------------------------------------------------------------
	const FResolvedConfigValue* FResolvedPhysicsConfig::Find(const std::string_view CanonicalKey) const
	{
		const auto It = Values.find(CanonicalKey);
		return It != Values.end() ? &It->second : nullptr;
	}

	std::string FResolvedPhysicsConfig::PhysicsSha256() const
	{
		FSHA256 Hasher;
		Hasher.Update("PhysicsTest.physics_sha256/1\n");
		for (const auto& [Key, Value] : Values)   // std::map: sorted, stable
		{
			if (Value.bPhysicsHash)
			{
				Hasher.Update(Key);
				Hasher.Update("=");
				Hasher.Update(Value.Value);
				Hasher.Update("\n");
			}
		}
		return Hasher.Final().ToString();
	}

	// ---- resolver ----------------------------------------------------------------------------------------------
	bool FProfileResolver::Resolve(const FProfileResolveInputs& Inputs, FResolvedPhysicsConfig& Out, std::string& OutError)
	{
		Out = FResolvedPhysicsConfig();
		if (Inputs.Base == nullptr || Inputs.Profile == nullptr)
		{
			OutError = "FProfileResolver::Resolve needs a base config and a physics profile";
			return false;
		}
		const FPhysicsProfile& Profile = *Inputs.Profile;
		Out.ProfileName = Profile.Name;
		if (Inputs.Variant != nullptr)
		{
			Out.Engine = Inputs.Variant->Engine;
			Out.VariantName = Inputs.Variant->Name;
		}
		std::vector<std::string> ImportWarnings;
		GImportWarnings = &ImportWarnings;
		struct FWarningScope
		{
			~FWarningScope() { GImportWarnings = nullptr; }
		} WarningScope;

		// 0. Built-in defaults: the code structs and the harness physics console variables.
		const auto SeedTable = [&Out](const ESectionKind Kind, const char* Prefix)
		{
			const void* Object = GetObject(Out, Kind);
			for (const FPropertyDesc& Desc : GetTable(Kind))
			{
				FResolvedConfigValue& Value = Out.Values[std::string(Prefix) + "." + Desc.Name];
				Value.Value = Desc.Export(Object);
				Value.Unit = Desc.Unit;
				Value.Origin = EConfigOrigin::UEDefault;
				Value.OriginName = "built-in";
			}
		};
		SeedTable(ESectionKind::PhysicsSettings, "PhysicsSettings");
		SeedTable(ESectionKind::PhysicsTestSettings, "PhysicsTestSettings");
		SeedTable(ESectionKind::BodyInstanceDefaults, "BodyInstanceDefaults");
		SeedTable(ESectionKind::PhysicalMaterialDefaults, "PhysicalMaterialDefaults");
		for (const IConsoleVariable* Variable : PhysicsCVars::GetHarnessConsoleVariables())
		{
			FResolvedConfigValue& Value = Out.Values[std::string("ConsoleVariables.") + Variable->GetName()];
			Value.Value = Variable->GetDefaultString();
			Value.Origin = EConfigOrigin::UEDefault;
			Value.OriginName = "built-in";
		}

		// Console variables the base config or the profile set are profile-owned: a variant may not change them unless
		// the profile marks them EngineDefault (otherwise a Chaos variant could undo a Normalized CVar).
		std::set<std::string> ProfileOwnedCVars;
		for (const FConfigFile* File : { Inputs.Base, &Profile.Ini })
		{
			if (const FConfigSection* Section = File->FindSection(ConfigSections::ConsoleVariables))
			{
				for (const FConfigSection::FEntry& Entry : Section->GetEntries())
				{
					ProfileOwnedCVars.insert(ToLower(Entry.Key));
				}
			}
		}

		FConfigFile CommandLineIni("-ini");
		if (!ParseIniOverrides(Inputs.CommandLine, CommandLineIni, Out.Warnings, OutError))
		{
			return false;
		}

		struct FLayer
		{
			const FConfigFile* File;
			EConfigOrigin Origin;
		};
		std::vector<FLayer> Layers = { { Inputs.Base, EConfigOrigin::UEDefault }, { &Profile.Ini, EConfigOrigin::PhysicsProfile } };
		if (Inputs.Variant != nullptr)
		{
			Layers.push_back({ &Inputs.Variant->Ini, EConfigOrigin::BackendVariant });
		}
		if (Inputs.ScenePinned != nullptr)
		{
			Layers.push_back({ Inputs.ScenePinned, EConfigOrigin::ScenePinned });
		}
		Layers.push_back({ &CommandLineIni, EConfigOrigin::CommandLine });

		FConfigFile Merged("merged");
		std::map<std::string, FOriginRecord> Origins;   // canonical key -> last setter
		for (const FLayer& Layer : Layers)
		{
			for (const FConfigLine& Line : Layer.File->GetLines())
			{
				const FSectionInfo Info = ClassifySection(Line.Section);
				const std::string Where = LineOrigin(*Layer.File, Line);
				const auto Fail = [&OutError, &Where](const std::string& Message)
				{
					OutError = Where + ": " + Message;
					return false;
				};

				// Which sections a layer may contain.
				switch (Info.Kind)
				{
				case ESectionKind::PhysicsSettingsCore:
					return Fail("[" + Line.Section + "] is not read: every UE physics key belongs to [" + ConfigSections::PhysicsSettings
						+ "], as in a UE game (BaseEngine.ini:3406, DL44)");
				case ESectionKind::ForcedOverrides:
					return Fail("[" + Line.Section + "] arrives in M9 (ForcedOverrides)");
				case ESectionKind::Unknown:
					return Fail("unknown section [" + Line.Section + "]");
				case ESectionKind::ProfileMeta:
					if (Layer.Origin != EConfigOrigin::PhysicsProfile)
					{
						return Fail("[" + Line.Section + "] belongs in a physics profile file only");
					}
					continue;
				case ESectionKind::VariantMeta:
					if (Layer.Origin != EConfigOrigin::BackendVariant)
					{
						return Fail("[" + Line.Section + "] belongs in a backend variant file only");
					}
					continue;
				case ESectionKind::Backend:
				case ESectionKind::BackendInit:
					if (Layer.Origin == EConfigOrigin::BackendVariant && Info.Engine != Inputs.Variant->Engine)
					{
						return Fail("backend variant '" + Inputs.Variant->Name + "' (" + Inputs.Variant->Engine + ") may not set " + Info.Engine + " keys");
					}
					if (Layer.Origin != EConfigOrigin::BackendVariant && Layer.Origin != EConfigOrigin::CommandLine)
					{
						return Fail("engine keys [" + Line.Section + "] belong in a backend variant (or a -ini: override)");
					}
					break;
				default:
					break;
				}
				if (Line.Key.empty())
				{
					continue;   // section declaration
				}
				if (Line.Op != EConfigLineOp::Set)
				{
					return Fail("'" + Line.Key + "' is a scalar setting; the array operators + . - ! are not valid here");
				}

				const std::string Canonical = MakeCanonicalKey(Line.Section, Line.Key);
				if (IsConfigKind(Info.Kind) && Info.Kind != ESectionKind::ConsoleVariables)
				{
					const FPropertyDesc* Desc = FindProperty(Info.Kind, Line.Key);
					if (Desc == nullptr && Info.Kind != ESectionKind::PhysicsSettings)
					{
						return Fail("unknown key '" + Line.Key + "' in [" + Line.Section + "]");
					}
					if (Desc != nullptr)
					{
						FResolvedPhysicsConfig Scratch;
						std::string ValueError;
						if (!Desc->Import(GetObject(Scratch, Info.Kind), Line.Value, ValueError))
						{
							return Fail(Line.Key + ": " + ValueError);
						}
					}
				}
				if (Info.Kind == ESectionKind::ConsoleVariables)
				{
					if (const IConsoleVariable* Variable = FindHarnessCVar(Line.Key))
					{
						std::string ValueError;
						if (!ValidateCVarValue(*Variable, Line.Value, ValueError))
						{
							return Fail(ValueError);
						}
					}
				}

				// The variant restriction (02 10): own engine keys + the keys the profile marks EngineDefault.
				if (Layer.Origin == EConfigOrigin::BackendVariant && IsConfigKind(Info.Kind))
				{
					bool bAllowed = Profile.GetKeyPolicy(Canonical) == EProfileKeyPolicy::EngineDefault;
					if (!bAllowed && Info.Kind == ESectionKind::ConsoleVariables)
					{
						// An engine console variable (not a harness physics variable, not set by the base config or the
						// profile) is an engine key.
						bAllowed = FindHarnessCVar(Line.Key) == nullptr && !ProfileOwnedCVars.contains(ToLower(Line.Key));
					}
					if (!bAllowed)
					{
						return Fail("backend variant '" + Inputs.Variant->Name + "' may not set pinned key " + Canonical + " (a variant sets only ["
							+ ConfigSections::BackendPrefix + Inputs.Variant->Engine + "] keys and the keys profile '" + Profile.Name
							+ "' lists as EngineDefaultKeys)");
					}
				}

				Merged.ApplyLine(Line, Layer.File->Name);
				Origins[Canonical] = FOriginRecord{ Layer.Origin, Where };
			}
		}

		// -ExecCmds="p.Foo 1, p.Bar=2" (console commands; UE runs them through the console, i.e. ECVF_SetByConsole).
		std::string ExecCmds;
		if (FParse::Value(Inputs.CommandLine.c_str(), "ExecCmds=", ExecCmds, false))
		{
			int32 Index = 0;
			for (const std::string& Full : SplitCommands(ExecCmds))
			{
				++Index;
				const std::string_view Trimmed = Trim(Full);
				const size_t Split = Trimmed.find_first_of(" \t=");
				if (Trimmed.empty())
				{
					continue;
				}
				if (Split == std::string_view::npos)
				{
					OutError = "-ExecCmds: '" + std::string(Trimmed) + "' has no value (expected \"cvar value\")";
					return false;
				}
				FConfigLine Line;
				Line.Section = ConfigSections::ConsoleVariables;
				Line.Key = std::string(Trim(Trimmed.substr(0, Split)));
				Line.Value = std::string(Trim(Trimmed.substr(Split + 1)));
				Line.LineNumber = Index;
				if (const IConsoleVariable* Variable = FindHarnessCVar(Line.Key))
				{
					std::string ValueError;
					if (!ValidateCVarValue(*Variable, Line.Value, ValueError))
					{
						OutError = "-ExecCmds: " + ValueError;
						return false;
					}
				}
				Merged.ApplyLine(Line, "-ExecCmds");
				Origins[MakeCanonicalKey(Line.Section, Line.Key)] = FOriginRecord{ EConfigOrigin::CommandLine, "-ExecCmds:" + std::to_string(Index) };
			}
		}

		// Materialise: structs, Values (with Origin), console variables, engine options, warnings.
		for (const std::string& SectionName : Merged.GetSectionNames())
		{
			const FSectionInfo Info = ClassifySection(SectionName);
			const FConfigSection* Section = Merged.FindSection(SectionName);
			for (const FConfigSection::FEntry& Entry : Section->GetEntries())
			{
				const std::string Canonical = MakeCanonicalKey(SectionName, Entry.Key);
				const FOriginRecord& Record = Origins[Canonical];
				FResolvedConfigValue& Value = Out.Values[Canonical];
				Value.Origin = Record.Origin;
				Value.OriginName = Record.Name;
				if (Info.Kind == ESectionKind::ConsoleVariables)
				{
					const IConsoleVariable* Variable = FindHarnessCVar(Entry.Key);
					Value.Value = Variable != nullptr ? NormalizeCVarValue(*Variable, Entry.Value.Value) : Entry.Value.Value;
					Out.ConsoleVariables.emplace_back(Variable != nullptr ? std::string(Variable->GetName()) : Entry.Key, Value.Value);
					continue;
				}
				if (Info.Kind == ESectionKind::Backend || Info.Kind == ESectionKind::BackendInit)
				{
					Value.Value = Entry.Value.Value;
					Value.bApplied = Info.Engine == Out.Engine;
					if (Value.bApplied)
					{
						(Info.Kind == ESectionKind::Backend ? Out.SceneOptions : Out.InitOptions).emplace_back(Entry.Key, Entry.Value.Value);
					}
					else
					{
						Out.Warnings.push_back(Canonical + " (" + Record.Name + ") is ignored: the resolved backend is '" + Out.Engine + "'");
					}
					continue;
				}
				const FPropertyDesc* Desc = FindProperty(Info.Kind, Entry.Key);
				if (Desc == nullptr)
				{
					// A UE key the harness does not model (e.g. pasted from a project's DefaultEngine.ini).
					Value.Value = Entry.Value.Value;
					Value.bApplied = false;
					Out.Warnings.push_back(Canonical + " (" + Record.Name + ") is a UE setting the harness does not model; kept for the record");
					continue;
				}
				void* Object = GetObject(Out, Info.Kind);
				std::string ValueError;
				if (!Desc->Import(Object, Entry.Value.Value, ValueError))
				{
					OutError = Record.Name + ": " + Entry.Key + ": " + ValueError;   // validated per line above; kept for safety
					return false;
				}
				Value.Value = Desc->Export(Object);
				Value.Unit = Desc->Unit;
			}
		}

		// physics_sha256 membership.
		for (auto& [Key, Value] : Out.Values)
		{
			const size_t Dot = Key.find('.');
			const std::string Prefix = Key.substr(0, Dot);
			const std::string Name = Key.substr(Dot + 1);
			bool bPhysics = false;
			if (Prefix == "ConsoleVariables")
			{
				bPhysics = FindHarnessCVar(Name) != nullptr || ProfileOwnedCVars.contains(ToLower(Name));
			}
			else if (Prefix != "Backend")
			{
				const FPropertyDesc* Desc = FindProperty(KindFromPrefix(Prefix), Name);
				bPhysics = Desc != nullptr && Desc->bPhysics;
			}
			Value.bPhysicsHash = bPhysics && Value.bApplied && Profile.GetKeyPolicy(Key) == EProfileKeyPolicy::Pinned;
		}

		// Consistency warnings: the harness tick mode is authoritative over UE's two tick switches.
		if (Out.Settings.bSubstepping != (Out.TestSettings.TickMode == EPhysicsTickMode::Substepping))
		{
			Out.Warnings.push_back(std::string("PhysicsSettings.bSubstepping=") + ExportText(Out.Settings.bSubstepping) + " is ignored: PhysicsTestSettings.TickMode="
				+ ToString(Out.TestSettings.TickMode) + " decides");
		}
		if (Out.Settings.bTickPhysicsAsync != (Out.TestSettings.TickMode == EPhysicsTickMode::AsyncFixed))
		{
			Out.Warnings.push_back(std::string("PhysicsSettings.bTickPhysicsAsync=") + ExportText(Out.Settings.bTickPhysicsAsync)
				+ " is ignored: PhysicsTestSettings.TickMode=" + ToString(Out.TestSettings.TickMode) + " decides");
		}
		// Struct fields are parsed twice (validation and application): report each unmodelled field once.
		std::set<std::string> Reported;
		for (std::string& Warning : ImportWarnings)
		{
			if (Reported.insert(Warning).second)
			{
				Out.Warnings.push_back(std::move(Warning));
			}
		}
		return true;
	}

	void FProfileResolver::ApplyBodyInstanceDefaults(const FResolvedPhysicsConfig& Config, FBodyInstance& InOutTemplate)
	{
		for (const FPropertyDesc& Desc : GBodyInstanceDefaultsProperties)
		{
			Desc.Copy(&InOutTemplate, &Config.BodyInstanceDefaults);
		}
	}

	void FProfileResolver::ApplyMaterialDefaults(const FResolvedPhysicsConfig& Config, FPhysicalMaterial& InOutTemplate)
	{
		for (const FPropertyDesc& Desc : GMaterialDefaultsProperties)
		{
			Desc.Copy(&InOutTemplate, &Config.MaterialDefaults);
		}
	}

	EConsoleVariableFlags FProfileResolver::GetConsoleVariableSetBy(const FResolvedConfigValue& Value)
	{
		if (Value.OriginName.starts_with("-ExecCmds"))
		{
			return ECVF_SetByConsole;
		}
		if (Value.Origin == EConfigOrigin::UEDefault && Value.OriginName == "built-in")
		{
			return ECVF_SetByConstructor;
		}
		return ECVF_SetByConsoleVariablesIni;
	}

	bool FProfileResolver::ApplyConsoleVariables(const FResolvedPhysicsConfig& Config, std::vector<std::pair<std::string, std::string>>& OutEngineVariables,
		std::string& OutError)
	{
		OutEngineVariables.clear();
		OutError.clear();
		for (const auto& [Name, Value] : Config.ConsoleVariables)
		{
			// The harness physics variables, then any other variable this process registers (viewer, tests); the rest
			// belongs to the engine.
			IConsoleVariable* Variable = FindHarnessCVar(Name);
			if (Variable == nullptr)
			{
				Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			}
			if (Variable == nullptr)
			{
				OutEngineVariables.emplace_back(Name, Value);
				continue;
			}
			// -ExecCmds with console priority, every ini layer (-ini: included) and built-in defaults as ini data (M2: -ini:
			// values used to be applied with console priority, unlike UE).
			const FResolvedConfigValue* Resolved = Config.Find("ConsoleVariables." + Name);
			const EConsoleVariableFlags SetBy = Resolved != nullptr && GetConsoleVariableSetBy(*Resolved) == ECVF_SetByConsole ? ECVF_SetByConsole
				: ECVF_SetByConsoleVariablesIni;
			if (Variable->Set(Value.c_str(), SetBy))
			{
				continue;
			}
			// UE logs and ignores a lower-priority Set; the harness reports it, so Values never claims a value the
			// variable does not hold.
			std::string Reason;
			if ((Variable->GetFlags() & ECVF_ReadOnly) != 0)
			{
				Reason = "the variable is read-only";
			}
			else if ((static_cast<uint32>(Variable->GetSetBy()) & ECVF_SetByMask) > (static_cast<uint32>(SetBy) & ECVF_SetByMask))
			{
				Reason = std::string("it is already set by ") + GetConsoleVariableSetByName(Variable->GetSetBy()) + ", which has priority over "
					+ GetConsoleVariableSetByName(SetBy);
			}
			else
			{
				Reason = "the value does not parse";
			}
			OutError += (OutError.empty() ? "" : "; ") + std::string(Variable->GetName()) + " = '" + Value + "' was not applied: " + Reason
				+ " (current value '" + Variable->GetString() + "')";
		}
		return OutError.empty();
	}
}
