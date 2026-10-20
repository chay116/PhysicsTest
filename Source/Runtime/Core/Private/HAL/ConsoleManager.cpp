// ConsoleManager.cpp - the console variable registry behind IConsoleManager::Get() (UE: Core/Private/HAL/ConsoleManager.cpp).
// Registration happens from static initialisers in any TU; the registry is a function-local static, so it exists
// before the first registration and outlives every registered variable.
#include "HAL/IConsoleManager.h"

#include "Logging/LogMacros.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>

namespace PhysicsTest
{
	namespace
	{
		char ToLowerAscii(const char C)
		{
			return (C >= 'A' && C <= 'Z') ? static_cast<char>(C - 'A' + 'a') : C;
		}

		std::string ToLowerKey(const std::string_view Name)
		{
			std::string Key(Name);
			for (char& C : Key)
			{
				C = ToLowerAscii(C);
			}
			return Key;
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

		bool EqualsNoCase(const std::string_view A, const char* B)
		{
			size_t Index = 0;
			for (; Index < A.size(); ++Index)
			{
				if (B[Index] == '\0' || ToLowerAscii(A[Index]) != ToLowerAscii(B[Index]))
				{
					return false;
				}
			}
			return B[Index] == '\0';
		}

		class FConsoleManager final : public IConsoleManager
		{
		public:
			IConsoleVariable* FindConsoleVariable(const std::string_view Name) const override
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				const auto It = Variables.find(ToLowerKey(Name));
				return It != Variables.end() ? It->second : nullptr;
			}

			bool ProcessUserConsoleInput(std::string_view Input, const EConsoleVariableFlags SetBy, std::string* OutMessage) override
			{
				Input = Trim(Input);
				size_t Split = Input.find_first_of(" \t=");
				const std::string_view Name = Input.substr(0, Split);
				std::string_view Value;
				if (Split != std::string_view::npos)
				{
					Value = Trim(Input.substr(Split + 1));
				}
				IConsoleVariable* Variable = FindConsoleVariable(Name);
				if (Variable == nullptr)
				{
					if (OutMessage != nullptr)
					{
						*OutMessage = "unknown console variable '" + std::string(Name) + "'";
					}
					return false;
				}
				if (Split == std::string_view::npos || Value.empty())
				{
					if (OutMessage != nullptr)
					{
						*OutMessage = std::string(Variable->GetName()) + " = \"" + Variable->GetString() + "\" (set by "
							+ GetConsoleVariableSetByName(Variable->GetSetBy()) + ")";
					}
					return true;
				}
				const std::string ValueText(Value);
				if (!Variable->Set(ValueText.c_str(), SetBy))
				{
					if (OutMessage != nullptr)
					{
						*OutMessage = std::string(Variable->GetName()) + ": value '" + ValueText + "' rejected (unparsable, read-only, or set by "
							+ GetConsoleVariableSetByName(Variable->GetSetBy()) + " which has priority over " + GetConsoleVariableSetByName(SetBy) + ")";
					}
					return false;
				}
				if (OutMessage != nullptr)
				{
					*OutMessage = std::string(Variable->GetName()) + " = \"" + Variable->GetString() + "\"";
				}
				return true;
			}

			void ForEachConsoleVariable(const std::function<void(IConsoleVariable&)>& Visitor) const override
			{
				std::map<std::string, IConsoleVariable*> Snapshot;
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					Snapshot = Variables;
				}
				for (const auto& Entry : Snapshot)
				{
					Visitor(*Entry.second);
				}
			}

			void RegisterConsoleVariable(IConsoleVariable* Variable) override
			{
				bool bDuplicate = false;
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					bDuplicate = !Variables.emplace(ToLowerKey(Variable->GetName()), Variable).second;
				}
				if (bDuplicate)
				{
					UE_LOG(LogCore, Fatal, "console variable '%s' is registered twice", Variable->GetName());
				}
			}

			void UnregisterConsoleVariable(IConsoleVariable* Variable) override
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				const auto It = Variables.find(ToLowerKey(Variable->GetName()));
				if (It != Variables.end() && It->second == Variable)
				{
					Variables.erase(It);
				}
			}

		private:
			mutable std::mutex Mutex;
			std::map<std::string, IConsoleVariable*> Variables;   // key: lower-case name
		};
	}

	IConsoleManager& IConsoleManager::Get()
	{
		static FConsoleManager Manager;
		return Manager;
	}

	const char* GetConsoleVariableSetByName(const EConsoleVariableFlags SetBy)
	{
		switch (static_cast<uint32>(SetBy) & ECVF_SetByMask)
		{
		case ECVF_SetByConstructor:         return "Constructor";
		case ECVF_SetByScalability:         return "Scalability";
		case ECVF_SetByGameSetting:         return "GameSetting";
		case ECVF_SetByProjectSetting:      return "ProjectSetting";
		case ECVF_SetBySystemSettingsIni:   return "SystemSettingsIni";
		case ECVF_SetByDeviceProfile:       return "DeviceProfile";
		case ECVF_SetByGameOverride:        return "GameOverride";
		case ECVF_SetByConsoleVariablesIni: return "ConsoleVariablesIni";
		case ECVF_SetByCommandline:         return "Commandline";
		case ECVF_SetByCode:                return "Code";
		case ECVF_SetByConsole:             return "Console";
		default:                            return "Unknown";
		}
	}

	namespace ConsoleVariablePrivate
	{
		bool ParseBool(std::string_view Text, bool& OutValue)
		{
			Text = Trim(Text);
			// UE FCString::ToBool (CString.cpp:123-153): True/Yes/On, False/No/Off, otherwise Atoi(Text) != 0.
			if (EqualsNoCase(Text, "true") || EqualsNoCase(Text, "yes") || EqualsNoCase(Text, "on"))
			{
				OutValue = true;
				return true;
			}
			if (EqualsNoCase(Text, "false") || EqualsNoCase(Text, "no") || EqualsNoCase(Text, "off"))
			{
				OutValue = false;
				return true;
			}
			int32 Number = 0;
			if (!ParseInt(Text, Number))
			{
				return false;
			}
			OutValue = Number != 0;
			return true;
		}

		bool ParseInt(std::string_view Text, int32& OutValue)
		{
			Text = Trim(Text);
			if (!Text.empty() && Text.front() == '+')
			{
				Text.remove_prefix(1);
			}
			int32 Value = 0;
			const auto [Ptr, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
			if (Text.empty() || Error != std::errc() || Ptr != Text.data() + Text.size())
			{
				return false;
			}
			OutValue = Value;
			return true;
		}

		bool ParseFloat(std::string_view Text, float& OutValue)
		{
			Text = Trim(Text);
			if (!Text.empty() && Text.front() == '+')
			{
				Text.remove_prefix(1);
			}
			float Value = 0.0f;
			const auto [Ptr, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
			// from_chars also reads "inf", "infinity" and "nan"; a console variable value must be a finite number.
			if (Text.empty() || Error != std::errc() || Ptr != Text.data() + Text.size() || !std::isfinite(Value))
			{
				return false;
			}
			OutValue = Value;
			return true;
		}

		std::string ToString(const bool Value)
		{
			return Value ? "1" : "0";   // UE prints bool console variables as 1 / 0 too
		}

		std::string ToString(const int32 Value)
		{
			return std::to_string(Value);
		}

		std::string ToString(const float Value)
		{
			char Buffer[32];
			const auto [Ptr, Error] = std::to_chars(Buffer, Buffer + sizeof(Buffer), Value);
			return Error == std::errc() ? std::string(Buffer, Ptr) : std::string("0");
		}

		std::string ToString(const std::string& Value)
		{
			return Value;
		}
	}

	FAutoConsoleVariableRef::FAutoConsoleVariableRef(const char* Name, bool& RefValue, const char* Help, const uint32 Flags)
		: Variable(new ConsoleVariablePrivate::TConsoleVariable<bool>(Name, RefValue, Help, Flags))
	{
	}

	FAutoConsoleVariableRef::FAutoConsoleVariableRef(const char* Name, int32& RefValue, const char* Help, const uint32 Flags)
		: Variable(new ConsoleVariablePrivate::TConsoleVariable<int32>(Name, RefValue, Help, Flags))
	{
	}

	FAutoConsoleVariableRef::FAutoConsoleVariableRef(const char* Name, float& RefValue, const char* Help, const uint32 Flags)
		: Variable(new ConsoleVariablePrivate::TConsoleVariable<float>(Name, RefValue, Help, Flags))
	{
	}

	FAutoConsoleVariableRef::~FAutoConsoleVariableRef()
	{
		delete Variable;
	}
}
