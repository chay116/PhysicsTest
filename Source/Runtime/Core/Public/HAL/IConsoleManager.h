// IConsoleManager.h - console variables (UE: Core/Public/HAL/IConsoleManager.h), minimal (DL18, S5).
//
//   static TAutoConsoleVariable<int32> CVarFoo("p.Foo", 1, "Help text");               // owns its value (no TEXT())
//   static bool bBar = true;
//   static FAutoConsoleVariableRef CVarBar("p.Bar", bBar, "Help text");               // refers to an existing variable
//   IConsoleManager::Get().FindConsoleVariable("p.Foo")->Set("2", ECVF_SetByConsole);
//
// What is kept from UE: the names above, case-insensitive lookup, value types bool / int32 / float / std::string, and
// the "set by" priority (ECVF_SetBy*, same order as IConsoleManager.h:150-187): a Set() with a lower priority than the
// current value's is ignored, as in UE. Not mirrored: console commands, sinks / callbacks, cheat and render-thread
// shadow values, tags, scalability groups. Harness additions: GetDefaultString() and ResetToDefault() (the profile
// resolver records CVar defaults with Origin UEDefault; unit tests restore global state).
// Text values: bool accepts true/false, yes/no, on/off and integers (non-zero = true, UE FCString::ToBool); float
// accepts finite numbers only (no inf / nan); a value that does not parse is rejected (Set returns false; UE would
// set 0).
#pragma once

#include "CoreTypes.h"

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

namespace PhysicsTest
{
	enum EConsoleVariableFlags : uint32
	{
		ECVF_Default = 0x0,
		ECVF_Cheat = 0x1,
		ECVF_ReadOnly = 0x4,

		ECVF_SetByMask = 0xff000000,
		ECVF_SetByConstructor = 0x00000000,
		ECVF_SetByScalability = 0x02000000,
		ECVF_SetByGameSetting = 0x03000000,
		ECVF_SetByProjectSetting = 0x04000000,
		ECVF_SetBySystemSettingsIni = 0x05000000,
		ECVF_SetByDeviceProfile = 0x07000000,
		ECVF_SetByGameOverride = 0x09000000,
		ECVF_SetByConsoleVariablesIni = 0x0A000000,
		ECVF_SetByCommandline = 0x0D000000,
		ECVF_SetByCode = 0x0E000000,
		ECVF_SetByConsole = 0x10000000,
	};

	[[nodiscard]] const char* GetConsoleVariableSetByName(EConsoleVariableFlags SetBy);

	enum class EConsoleVariableType : uint8
	{
		Bool,
		Int,
		Float,
		String,
	};

	class IConsoleVariable
	{
	public:
		virtual ~IConsoleVariable() = default;

		[[nodiscard]] virtual const char* GetName() const = 0;
		[[nodiscard]] virtual const char* GetHelp() const = 0;
		[[nodiscard]] virtual EConsoleVariableType GetType() const = 0;
		[[nodiscard]] virtual EConsoleVariableFlags GetFlags() const = 0;
		[[nodiscard]] EConsoleVariableFlags GetSetBy() const { return static_cast<EConsoleVariableFlags>(GetFlags() & ECVF_SetByMask); }

		// Returns false (value unchanged) when the text does not parse, the variable is read-only, or SetBy has a lower
		// priority than the current value's (UE: ignored with a log line).
		virtual bool Set(const char* InValue, EConsoleVariableFlags SetBy = ECVF_SetByCode) = 0;

		[[nodiscard]] virtual bool GetBool() const = 0;
		[[nodiscard]] virtual int32 GetInt() const = 0;
		[[nodiscard]] virtual float GetFloat() const = 0;
		[[nodiscard]] virtual std::string GetString() const = 0;

		// Harness additions.
		[[nodiscard]] virtual std::string GetDefaultString() const = 0;
		virtual void ResetToDefault() = 0;
	};

	class IConsoleManager
	{
	public:
		[[nodiscard]] static IConsoleManager& Get();

		virtual ~IConsoleManager() = default;

		// Case-insensitive (UE). Returns nullptr when unknown.
		[[nodiscard]] virtual IConsoleVariable* FindConsoleVariable(std::string_view Name) const = 0;
		// "p.Foo 1", "p.Foo=1" or "p.Foo" (the last only reports). Returns false for an unknown variable or a rejected value.
		virtual bool ProcessUserConsoleInput(std::string_view Input, EConsoleVariableFlags SetBy, std::string* OutMessage = nullptr) = 0;
		virtual void ForEachConsoleVariable(const std::function<void(IConsoleVariable&)>& Visitor) const = 0;

		// Registration happens through TAutoConsoleVariable / FAutoConsoleVariableRef. Registering a name twice is Fatal.
		virtual void RegisterConsoleVariable(IConsoleVariable* Variable) = 0;
		virtual void UnregisterConsoleVariable(IConsoleVariable* Variable) = 0;
	};

	namespace ConsoleVariablePrivate
	{
		// Text conversion shared by the typed variables. Return false when Text does not parse.
		bool ParseBool(std::string_view Text, bool& OutValue);
		bool ParseInt(std::string_view Text, int32& OutValue);
		bool ParseFloat(std::string_view Text, float& OutValue);
		std::string ToString(bool Value);
		std::string ToString(int32 Value);
		std::string ToString(float Value);
		std::string ToString(const std::string& Value);

		template <typename T>
		constexpr EConsoleVariableType TypeOf()
		{
			if constexpr (std::is_same_v<T, bool>)
			{
				return EConsoleVariableType::Bool;
			}
			else if constexpr (std::is_same_v<T, int32>)
			{
				return EConsoleVariableType::Int;
			}
			else if constexpr (std::is_same_v<T, float>)
			{
				return EConsoleVariableType::Float;
			}
			else
			{
				static_assert(std::is_same_v<T, std::string>, "console variables are bool, int32, float or std::string");
				return EConsoleVariableType::String;
			}
		}

		template <typename T>
		bool Parse(const std::string_view Text, T& OutValue)
		{
			if constexpr (std::is_same_v<T, bool>)
			{
				return ParseBool(Text, OutValue);
			}
			else if constexpr (std::is_same_v<T, int32>)
			{
				return ParseInt(Text, OutValue);
			}
			else if constexpr (std::is_same_v<T, float>)
			{
				return ParseFloat(Text, OutValue);
			}
			else
			{
				OutValue = std::string(Text);
				return true;
			}
		}

		// Common implementation over a value stored either in the variable (TAutoConsoleVariable) or elsewhere
		// (FAutoConsoleVariableRef).
		template <typename T>
		class TConsoleVariable final : public IConsoleVariable
		{
		public:
			TConsoleVariable(const char* InName, T& InStorage, const char* InHelp, const uint32 InFlags)
				: Name(InName), Help(InHelp != nullptr ? InHelp : ""), Storage(InStorage), DefaultValue(InStorage),
				Flags(static_cast<EConsoleVariableFlags>((InFlags & ~ECVF_SetByMask) | ECVF_SetByConstructor))
			{
				IConsoleManager::Get().RegisterConsoleVariable(this);
			}
			~TConsoleVariable() override { IConsoleManager::Get().UnregisterConsoleVariable(this); }
			TConsoleVariable(const TConsoleVariable&) = delete;
			TConsoleVariable& operator=(const TConsoleVariable&) = delete;

			const char* GetName() const override { return Name; }
			const char* GetHelp() const override { return Help; }
			EConsoleVariableType GetType() const override { return TypeOf<T>(); }
			EConsoleVariableFlags GetFlags() const override { return Flags; }

			bool Set(const char* InValue, const EConsoleVariableFlags SetBy) override
			{
				const uint32 NewSetBy = static_cast<uint32>(SetBy) & ECVF_SetByMask;
				const uint32 OldSetBy = static_cast<uint32>(Flags) & ECVF_SetByMask;
				if ((Flags & ECVF_ReadOnly) != 0 || NewSetBy < OldSetBy || InValue == nullptr)
				{
					return false;
				}
				T Parsed{};
				if (!Parse(std::string_view(InValue), Parsed))
				{
					return false;
				}
				Storage = Parsed;
				Flags = static_cast<EConsoleVariableFlags>((static_cast<uint32>(Flags) & ~ECVF_SetByMask) | NewSetBy);
				return true;
			}

			bool GetBool() const override
			{
				if constexpr (std::is_same_v<T, std::string>)
				{
					bool Value = false;
					return ParseBool(Storage, Value) && Value;
				}
				else
				{
					return Storage != T{};
				}
			}
			int32 GetInt() const override
			{
				if constexpr (std::is_same_v<T, std::string>)
				{
					int32 Value = 0;
					return ParseInt(Storage, Value) ? Value : 0;
				}
				else
				{
					return static_cast<int32>(Storage);
				}
			}
			float GetFloat() const override
			{
				if constexpr (std::is_same_v<T, std::string>)
				{
					float Value = 0.0f;
					return ParseFloat(Storage, Value) ? Value : 0.0f;
				}
				else
				{
					return static_cast<float>(Storage);
				}
			}
			std::string GetString() const override { return ToString(Storage); }
			std::string GetDefaultString() const override { return ToString(DefaultValue); }
			void ResetToDefault() override
			{
				Storage = DefaultValue;
				Flags = static_cast<EConsoleVariableFlags>((static_cast<uint32>(Flags) & ~ECVF_SetByMask) | ECVF_SetByConstructor);
			}

			[[nodiscard]] const T& GetValue() const { return Storage; }

		private:
			const char* Name;
			const char* Help;
			T& Storage;
			const T DefaultValue;
			EConsoleVariableFlags Flags;
		};
	}

	// Owns its value (UE: TAutoConsoleVariable<T>). GetValueOnGameThread() is the only read path the harness needs.
	template <typename T>
	class TAutoConsoleVariable
	{
	public:
		TAutoConsoleVariable(const char* Name, const T& DefaultValue, const char* Help, const uint32 Flags = ECVF_Default)
			: Value(DefaultValue), Variable(Name, Value, Help, Flags)
		{
		}

		[[nodiscard]] const T& GetValueOnGameThread() const { return Value; }
		[[nodiscard]] IConsoleVariable* AsVariable() { return &Variable; }
		[[nodiscard]] IConsoleVariable* operator->() { return &Variable; }

	private:
		T Value;
		ConsoleVariablePrivate::TConsoleVariable<T> Variable;
	};

	// Refers to an existing variable (UE: FAutoConsoleVariableRef). The variable's value at registration is the default.
	class FAutoConsoleVariableRef
	{
	public:
		FAutoConsoleVariableRef(const char* Name, bool& RefValue, const char* Help, uint32 Flags = ECVF_Default);
		FAutoConsoleVariableRef(const char* Name, int32& RefValue, const char* Help, uint32 Flags = ECVF_Default);
		FAutoConsoleVariableRef(const char* Name, float& RefValue, const char* Help, uint32 Flags = ECVF_Default);
		~FAutoConsoleVariableRef();
		FAutoConsoleVariableRef(const FAutoConsoleVariableRef&) = delete;
		FAutoConsoleVariableRef& operator=(const FAutoConsoleVariableRef&) = delete;

		[[nodiscard]] IConsoleVariable* AsVariable() { return Variable; }
		[[nodiscard]] IConsoleVariable* operator->() { return Variable; }

	private:
		IConsoleVariable* Variable = nullptr;
	};
}
