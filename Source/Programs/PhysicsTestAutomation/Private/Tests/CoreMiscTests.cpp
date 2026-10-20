// CoreMiscTests.cpp - hashes (published test vectors), FParse / FCommandLine (UE semantics), FRandomStream, QPC clock.
#include "CoreMinimal.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <string>

using namespace PhysicsTest;

TEST_CASE("PhysicsTest.Core.Sha256KnownVectors")
{
	// FIPS 180-4 / NIST CAVP examples.
	CHECK(FSHA256::HashString("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(FSHA256::HashString("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(FSHA256::HashString("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
		== "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	// Incremental updates across block boundaries give the same digest.
	const std::string Million(1000000, 'a');
	FSHA256 Hasher;
	for (size_t Offset = 0; Offset < Million.size(); Offset += 997)
	{
		Hasher.Update(Million.data() + Offset, std::min<size_t>(997, Million.size() - Offset));
	}
	CHECK(Hasher.Final().ToString() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("PhysicsTest.Core.Fnv1aKnownVectors")
{
	CHECK(FFnv::MemFnv32("", 0) == 0x811c9dc5u);
	CHECK(FFnv::MemFnv32("a", 1) == 0xe40c292cu);
	CHECK(FFnv::MemFnv32("foobar", 6) == 0xbf9cf968u);
	CHECK(FFnv::MemFnv64("", 0) == 0xcbf29ce484222325ull);
	CHECK(FFnv::MemFnv64("a", 1) == 0xaf63dc4c8601ec8cull);
	CHECK(FFnv::MemFnv64("foobar", 6) == 0x85944171f73967e8ull);
}

TEST_CASE("PhysicsTest.Core.ParseParam")
{
	const char* Stream = "-Scene=M3.BoxPile -Bench /Viz -Engine=Jolt";
	CHECK(FParse::Param(Stream, "Bench"));
	CHECK(FParse::Param(Stream, "-bench"));     // case-insensitive, leading '-' ignored
	CHECK(FParse::Param(Stream, "Viz"));        // '/' prefix
	CHECK_FALSE(FParse::Param(Stream, "Ben"));  // whole token only
	CHECK_FALSE(FParse::Param(Stream, "Engine"));
	CHECK_FALSE(FParse::Param("Bench", "Bench"));   // needs the '-' or '/' prefix
	CHECK_FALSE(FParse::Param("x-Bench", "Bench"));   // the prefix must start the token
	CHECK_FALSE(FParse::Param("-Bench2", "Bench"));
	CHECK_FALSE(FParse::Param("-NoBench", "Bench"));
	CHECK(FParse::Param("-Label=x\t-Bench\r\n", "Bench"));   // any whitespace separates tokens
	CHECK(FParse::Param("-a -Bench", "Bench"));   // a later occurrence counts
	CHECK_FALSE(FParse::Param("-Label=\"a -Bench b\"", "Bench"));   // inside quotes (UE: Strifind skips quoted text)
	CHECK(FParse::Param("-Label=\"a b\" -Bench", "Bench"));
	CHECK_FALSE(FParse::Param("-Bench", ""));
}

TEST_CASE("PhysicsTest.Core.ParseKeyNeedsNonAlnumLeadIn")
{
	// UE FCString::Strifind only matches where the previous character is not [A-Za-z0-9] (CString.h:747), so a key
	// is never found inside a longer option name. The bench CLI relies on this (-Steps / -WarmupSteps, ...).
	int32 Steps = 0;
	CHECK(FParse::Value("-WarmupSteps=10 -Steps=600", "Steps=", Steps));
	CHECK(Steps == 600);
	CHECK(FParse::Value("-Steps=600 -WarmupSteps=10", "Steps=", Steps));
	CHECK(Steps == 600);
	int32 Missing = 7;
	CHECK_FALSE(FParse::Value("-WarmupSteps=10", "Steps=", Missing));
	CHECK(Missing == 7);

	std::string Text;
	CHECK(FParse::Value("-PhysXThreads=4 -Threads=1", "Threads=", Text));
	CHECK(Text == "1");
	CHECK(FParse::Value("-BenchScene=Other -Scene=M3.BoxPile", "Scene=", Text));
	CHECK(Text == "M3.BoxPile");
	CHECK(FParse::Value("Scene=AtStart", "Scene=", Text));   // the start of the stream is a valid lead-in
	CHECK(Text == "AtStart");

	// '_' and '-' are not alphanumeric: UE matches after them too.
	CHECK(FParse::Value("-Warmup_Steps=5", "Steps=", Steps));
	CHECK(Steps == 5);

	// Text inside double quotes is skipped while searching for the key.
	CHECK(FParse::Value("-Label=\"x -Steps=3\" -Steps=7", "Steps=", Steps));
	CHECK(Steps == 7);
	CHECK_FALSE(FParse::Value("-Steps=1", "", Text));   // an empty key never matches (UE)
}

TEST_CASE("PhysicsTest.Core.ParseValue")
{
	const char* Stream = "-Scene=M3.BoxPile -Threads=4 -Dt=0.016666 -Label=\"two words\" -Big=12345678901 -Cpu=1,2";
	std::string Text;
	CHECK(FParse::Value(Stream, "Scene=", Text));
	CHECK(Text == "M3.BoxPile");
	CHECK(FParse::Value(Stream, "label=", Text));   // case-insensitive
	CHECK(Text == "two words");
	CHECK(FParse::Value(Stream, "Cpu=", Text));     // stops on ',' by default (UE bShouldStopOnSeparator)
	CHECK(Text == "1");
	CHECK(FParse::Value(Stream, "Cpu=", Text, false));
	CHECK(Text == "1,2");

	int32 Threads = 0;
	CHECK(FParse::Value(Stream, "Threads=", Threads));
	CHECK(Threads == 4);
	int64 Big = 0;
	CHECK(FParse::Value(Stream, "Big=", Big));
	CHECK(Big == 12345678901ll);
	double Dt = 0.0;
	CHECK(FParse::Value(Stream, "Dt=", Dt));
	CHECK(Dt == doctest::Approx(0.016666));
	int32 Missing = 7;
	CHECK_FALSE(FParse::Value(Stream, "Missing=", Missing));
	CHECK(Missing == 7);
	CHECK_FALSE(FParse::Value("-Threads=abc", "Threads=", Missing));

	bool bFlag = false;
	CHECK(FParse::Bool("-Sleep=On", "Sleep=", bFlag));
	CHECK(bFlag);
	CHECK(FParse::Bool("-Sleep=false", "Sleep=", bFlag));
	CHECK_FALSE(bFlag);
	CHECK(FParse::Bool("-Sleep=2", "Sleep=", bFlag));   // integers: non-zero is true (UE FCString::ToBool -> Atoi)
	CHECK(bFlag);
	CHECK(FParse::Bool("-Sleep=0", "Sleep=", bFlag));
	CHECK_FALSE(bFlag);
	CHECK(FParse::Bool("-Sleep=YES", "Sleep=", bFlag));
	CHECK(bFlag);
	bFlag = true;
	CHECK_FALSE(FParse::Bool("-Sleep=maybe", "Sleep=", bFlag));   // harness: rejected, OnOff unchanged (UE: true, false)
	CHECK(bFlag);
}

TEST_CASE("PhysicsTest.Core.CommandLine")
{
	char Arg0[] = "PhysicsTestBench.exe";
	char Arg1[] = "-Scene=M3.BoxPile";
	char Arg2[] = "-Label=two words";
	char Arg3[] = "-Bench";
	char* ArgV[] = { Arg0, Arg1, Arg2, Arg3 };
	const std::string Built = FCommandLine::BuildFromArgV(nullptr, 4, ArgV, nullptr);
	CHECK(Built == "-Scene=M3.BoxPile -Label=\"two words\" -Bench");

	const std::string Saved = FCommandLine::Get();
	FCommandLine::Set(Built.c_str());
	FCommandLine::Append(" -Extra");
	std::string Label;
	CHECK(FParse::Value(FCommandLine::Get(), "Label=", Label));
	CHECK(Label == "two words");
	CHECK(FParse::Param(FCommandLine::Get(), "Extra"));
	FCommandLine::Set(Saved.c_str());
}

TEST_CASE("PhysicsTest.Core.RandomStream")
{
	FRandomStream A;
	FRandomStream B(FRandomStream::DefaultSeed);
	CHECK(A.GetInitialSeed() == 0x5EED);
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		const double FA = A.GetFraction();
		CHECK(FA == B.GetFraction());
		CHECK(FA >= 0.0);
		CHECK(FA < 1.0);
		const int32 RA = A.RandRange(-3, 3);
		CHECK(RA == B.RandRange(-3, 3));
		CHECK(RA >= -3);
		CHECK(RA <= 3);
		const float F = A.FRand();
		CHECK(F == B.FRand());
		CHECK(F < 1.0f);
	}
	const double First = []
	{
		FRandomStream S(42);
		return S.GetFraction();
	}();
	FRandomStream C(42);
	(void)C.GetFraction();
	C.Reset();
	CHECK(C.GetFraction() == First);
}

TEST_CASE("PhysicsTest.Core.PlatformTimeIsQpc")
{
	CHECK(FPlatformTime::GetCyclesPerSecond64() > 0u);
	CHECK(FPlatformTime::GetSecondsPerCycle64() == doctest::Approx(1.0 / double(FPlatformTime::GetCyclesPerSecond64())));
	const uint64 T0 = FPlatformTime::Cycles64();
	const uint64 T1 = FPlatformTime::Cycles64();
	CHECK(T1 >= T0);
}

TEST_CASE("PhysicsTest.Core.EnsureReturnsFalseOnce")
{
#if PT_DO_CHECK
	int32 Failures = 0;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!ensureMsgf(Index < 0, "expected failure %d (logged once)", Index))
		{
			++Failures;
		}
	}
	CHECK(Failures == 3);
#else
	CHECK(ensure(true));
	CHECK_FALSE(ensure(false));
#endif
}
