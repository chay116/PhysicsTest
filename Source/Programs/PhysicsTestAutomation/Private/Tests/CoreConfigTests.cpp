// CoreConfigTests.cpp - FConfigFile (ini syntax, layering, provenance) and IConsoleManager (types, set-by priority).
#include "CoreMinimal.h"

#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <string>
#include <vector>

using namespace PhysicsTest;

namespace
{
	// Test-only console variables (registered for the whole Automation process).
	TAutoConsoleVariable<int32> CVarTestInt("pt.Test.Int", 3, "unit test variable");
	TAutoConsoleVariable<float> CVarTestFloat("pt.Test.Float", 0.5f, "unit test variable");
	TAutoConsoleVariable<std::string> CVarTestString("pt.Test.String", std::string("abc"), "unit test variable");
	bool bTestRefBool = true;
	FAutoConsoleVariableRef CVarTestRefBool("pt.Test.RefBool", bTestRefBool, "unit test variable");
}

TEST_CASE("PhysicsTest.Config.IniSyntaxAndArrayOps")
{
	const char* Text =
		"; comment\n"
		"# also a comment\n"
		"[Section A]\n"
		"Key=Value\n"
		"  Spaced  =  padded value  \n"
		"Quoted=\"with \\\"escape\\\" and ; semicolon\"\n"
		"+Array=1\n"
		"+Array=2\n"
		"+Array=1\n"         // AddUnique: already there
		".Array=1\n"         // Add: duplicates allowed
		"-Array=2\n"         // Remove
		"Key=Replaced\n"     // Set replaces every value of Key
		"[/Script/Engine.PhysicsSettings]\n"
		"DefaultGravityZ=-980.0\n"
		"[Empty]\n";
	FConfigFile File("test.ini");
	std::string Error;
	REQUIRE(File.ParseText(Text, Error));
	std::string Value;
	CHECK(File.GetString("Section A", "Key", Value));
	CHECK(Value == "Replaced");
	CHECK(File.GetString("Section A", "key", Value));   // keys are case-insensitive
	CHECK(File.GetString("Section A", "Spaced", Value));
	CHECK(Value == "padded value");
	CHECK(File.GetString("Section A", "Quoted", Value));
	CHECK(Value == "with \"escape\" and ; semicolon");
	std::vector<std::string> Array;
	CHECK(File.GetArray("Section A", "Array", Array));
	CHECK(Array == std::vector<std::string>{ "1", "1" });
	float Gravity = 0.0f;
	CHECK(File.GetFloat("/Script/Engine.PhysicsSettings", "DefaultGravityZ", Gravity));
	CHECK(Gravity == -980.0f);
	// Section names are case-insensitive as in UE (TMap<FString, FConfigSection>); the first spelling is kept.
	REQUIRE(File.FindSection("section a") != nullptr);
	CHECK(File.FindSection("section a") == File.FindSection("Section A"));
	CHECK(File.GetString("SECTION A", "KEY", Value));
	REQUIRE(File.FindSection("Empty") != nullptr);
	CHECK(File.FindSection("Empty")->IsEmpty());
	CHECK(File.GetSectionNames() == std::vector<std::string>{ "Section A", "/Script/Engine.PhysicsSettings", "Empty" });

	// Provenance: the value remembers the file and line that set it.
	const FConfigValue* KeyValue = File.FindSection("Section A")->Find("Key");
	REQUIRE(KeyValue != nullptr);
	CHECK(KeyValue->SourceName == "test.ini");
	CHECK(KeyValue->LineNumber == 12);

	// Clear (the upper file spells the section differently: it is the same section).
	FConfigFile Upper("upper.ini");
	REQUIRE(Upper.ParseText("[section a]\n!Array=\nKey=Upper\n", Error));
	File.Combine(Upper);
	CHECK_FALSE(File.GetArray("Section A", "Array", Array));
	CHECK(File.GetString("Section A", "Key", Value));
	CHECK(Value == "Upper");
	CHECK(File.FindSection("Section A")->Find("Key")->SourceName == "upper.ini");
	CHECK(File.GetSectionNames() == std::vector<std::string>{ "Section A", "/Script/Engine.PhysicsSettings", "Empty" });

	// Numbers: finite only.
	FConfigFile Numbers("numbers.ini");
	REQUIRE(Numbers.ParseText("[N]\nInf=inf\nNan=nan\nBig=1e999\nOk=-980.5\n", Error));
	double D = 1.0;
	CHECK_FALSE(Numbers.GetDouble("N", "Inf", D));
	CHECK_FALSE(Numbers.GetDouble("N", "Nan", D));
	CHECK_FALSE(Numbers.GetDouble("N", "Big", D));
	CHECK_FALSE(Numbers.GetFloat("N", "Inf", Gravity));
	CHECK(Numbers.GetDouble("N", "Ok", D));
	CHECK(D == -980.5);
}

TEST_CASE("PhysicsTest.Config.IniBomCrLfAndErrors")
{
	FConfigFile File("bom.ini");
	std::string Error;
	REQUIRE(File.ParseText("\xEF\xBB\xBF[S]\r\nA=1\r\nB=2\r\n", Error));
	int32 A = 0;
	int32 B = 0;
	CHECK(File.GetInt("S", "A", A));
	CHECK(File.GetInt("S", "B", B));
	CHECK(A == 1);
	CHECK(B == 2);

	FConfigFile Bad1("bad1.ini");
	CHECK_FALSE(Bad1.ParseText("A=1\n", Error));
	CHECK(Error.find("bad1.ini:1") != std::string::npos);
	FConfigFile Bad2("bad2.ini");
	CHECK_FALSE(Bad2.ParseText("[S]\n\nNoEquals\n", Error));
	CHECK(Error.find("bad2.ini:3") != std::string::npos);
	FConfigFile Bad3("bad3.ini");
	CHECK_FALSE(Bad3.ParseText("[Unclosed\n", Error));
	FConfigFile Bad4("bad4.ini");
	CHECK_FALSE(Bad4.ParseText("[S]\n=value\n", Error));

	std::string Missing;
	FConfigFile NotThere;
	CHECK_FALSE(FConfigFile::LoadFile("Z:/definitely/missing.ini", NotThere, Missing));
}

TEST_CASE("PhysicsTest.Config.ConsoleVariableTypesAndPriority")
{
	IConsoleManager& Manager = IConsoleManager::Get();
	IConsoleVariable* Int = Manager.FindConsoleVariable("PT.TEST.INT");   // case-insensitive
	REQUIRE(Int != nullptr);
	CHECK(Int->GetType() == EConsoleVariableType::Int);
	CHECK(Int->GetInt() == 3);
	CHECK(Int->GetDefaultString() == "3");
	CHECK(Int->GetSetBy() == ECVF_SetByConstructor);

	CHECK(Int->Set("7", ECVF_SetByConsoleVariablesIni));
	CHECK(CVarTestInt.GetValueOnGameThread() == 7);
	CHECK_FALSE(Int->Set("8", ECVF_SetByProjectSetting));   // lower priority than ConsoleVariablesIni: ignored (UE)
	CHECK(Int->GetInt() == 7);
	CHECK(Int->Set("9", ECVF_SetByConsole));
	CHECK(Int->GetInt() == 9);
	CHECK_FALSE(Int->Set("nine", ECVF_SetByConsole));   // unparsable: rejected, value kept
	CHECK(Int->GetInt() == 9);
	IConsoleVariable* Float = Manager.FindConsoleVariable("pt.Test.Float");
	REQUIRE(Float != nullptr);
	CHECK_FALSE(Float->Set("nan", ECVF_SetByConsole));   // float variables take finite numbers only
	CHECK_FALSE(Float->Set("inf", ECVF_SetByConsole));
	CHECK(Float->GetFloat() == 0.5f);
	Int->ResetToDefault();
	CHECK(Int->GetInt() == 3);
	CHECK(Int->GetSetBy() == ECVF_SetByConstructor);

	std::string Message;
	CHECK(Manager.ProcessUserConsoleInput("pt.test.float 2.25", ECVF_SetByConsole, &Message));
	CHECK(CVarTestFloat.GetValueOnGameThread() == 2.25f);
	CHECK(Manager.ProcessUserConsoleInput("pt.Test.String=hello world", ECVF_SetByConsole, &Message));
	CHECK(CVarTestString.GetValueOnGameThread() == "hello world");
	CHECK(Manager.ProcessUserConsoleInput("pt.Test.RefBool off", ECVF_SetByConsole, &Message));
	CHECK_FALSE(bTestRefBool);
	CHECK(Manager.ProcessUserConsoleInput("pt.Test.RefBool", ECVF_SetByConsole, &Message));   // query only
	CHECK(Message.find("pt.Test.RefBool") != std::string::npos);
	CHECK_FALSE(Manager.ProcessUserConsoleInput("pt.Test.Unknown 1", ECVF_SetByConsole, &Message));
	CHECK(Message.find("unknown") != std::string::npos);

	int32 Count = 0;
	Manager.ForEachConsoleVariable([&Count](IConsoleVariable& Variable)
	{
		if (std::string(Variable.GetName()).starts_with("pt.Test."))
		{
			++Count;
		}
	});
	CHECK(Count == 4);

	Manager.FindConsoleVariable("pt.Test.Float")->ResetToDefault();
	Manager.FindConsoleVariable("pt.Test.String")->ResetToDefault();
	Manager.FindConsoleVariable("pt.Test.RefBool")->ResetToDefault();
	CHECK(CVarTestFloat.GetValueOnGameThread() == 0.5f);
	CHECK(CVarTestString.GetValueOnGameThread() == "abc");
	CHECK(bTestRefBool);
}
