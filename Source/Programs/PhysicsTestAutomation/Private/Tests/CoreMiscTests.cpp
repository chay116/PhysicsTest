// CoreMiscTests.cpp - hashes (published test vectors), FParse / FCommandLine (UE semantics), UTF-8 conversion,
// FRandomStream (golden values), QPC clock.
#include "CoreMinimal.h"

#include "Containers/StringConv.h"

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

TEST_CASE("PhysicsTest.Core.ParseNumericRangeAndSign")
{
	// Unsigned overloads reject a '-' sign instead of wrapping around (strtoul("-1") = 4294967295 without ERANGE).
	uint32 Threads = 7;
	CHECK_FALSE(FParse::Value("-Threads=-1", "Threads=", Threads));
	CHECK(Threads == 7u);
	CHECK_FALSE(FParse::Value("-Threads= -1", "Threads=", Threads));
	CHECK(Threads == 7u);
	uint64 Mask = 9;
	CHECK_FALSE(FParse::Value("-Affinity=-5", "Affinity=", Mask));
	CHECK(Mask == 9u);

	// Overflow is rejected, never truncated.
	CHECK_FALSE(FParse::Value("-Threads=4294967296", "Threads=", Threads));
	CHECK(FParse::Value("-Threads=4294967295", "Threads=", Threads));
	CHECK(Threads == 4294967295u);
	CHECK_FALSE(FParse::Value("-Mask=18446744073709551616", "Mask=", Mask));
	CHECK(FParse::Value("-Mask=18446744073709551615", "Mask=", Mask));
	CHECK(Mask == 18446744073709551615ull);
	int32 Signed = 3;
	CHECK_FALSE(FParse::Value("-N=2147483648", "N=", Signed));
	CHECK_FALSE(FParse::Value("-N=-2147483649", "N=", Signed));
	CHECK(Signed == 3);
	CHECK(FParse::Value("-N=-2147483648", "N=", Signed));
	CHECK(Signed == -2147483647 - 1);
	int64 Big = 0;
	CHECK_FALSE(FParse::Value("-N=9223372036854775808", "N=", Big));
	CHECK(FParse::Value("-N=-9223372036854775808", "N=", Big));
	CHECK(FParse::Value("-Threads=+4", "Threads=", Threads));   // an explicit '+' is fine
	CHECK(Threads == 4u);

	// float / double: a real, finite number (strtod alone would accept inf / nan).
	double Dt = 0.25;
	float Scale = 0.5f;
	for (const char* Text : { "-Dt=inf", "-Dt=-INF", "-Dt=nan", "-Dt=infinity", "-Dt=1e999" })
	{
		CAPTURE(Text);
		CHECK_FALSE(FParse::Value(Text, "Dt=", Dt));
		CHECK_FALSE(FParse::Value(Text, "Dt=", Scale));
	}
	CHECK(Dt == 0.25);
	CHECK(Scale == 0.5f);
	CHECK(FParse::Value("-Dt=0.016666668", "Dt=", Dt));
	CHECK(Dt == doctest::Approx(0.016666668));
}

TEST_CASE("PhysicsTest.Core.ParseToken")
{
	// UE FParse::Token: a leading quote is stripped, quotes inside a token are kept, a delimiter can be given.
	const char* Stream = "  -A=1 \"two words\" -Label=\"x y\"  -ini:PhysicsTest:[S]:K=V,[S]:K2=\"a,b\"";
	std::string Token;
	CHECK(FParse::Token(Stream, Token));
	CHECK(Token == "-A=1");
	CHECK(FParse::Token(Stream, Token));
	CHECK(Token == "two words");
	CHECK(FParse::Token(Stream, Token));
	CHECK(Token == "-Label=\"x y\"");
	CHECK(FParse::Token(Stream, Token));
	CHECK(Token == "-ini:PhysicsTest:[S]:K=V,[S]:K2=\"a,b\"");
	CHECK_FALSE(FParse::Token(Stream, Token));

	const std::string Settings = "[S]:K=V,[S]:K2=\"a,b\"";
	const char* SettingsStream = Settings.c_str();
	CHECK(FParse::Token(SettingsStream, Token, ','));
	CHECK(Token == "[S]:K=V");
	CHECK(FParse::Token(SettingsStream, Token, ','));
	CHECK(Token == "[S]:K2=\"a,b\"");   // the comma inside quotes does not split
	CHECK_FALSE(FParse::Token(SettingsStream, Token, ','));
}

TEST_CASE("PhysicsTest.Core.StringConvUtf8")
{
	// "한글 경로" (Korean path) in UTF-16 and UTF-8.
	const wchar_t Wide[] = { 0xD55C, 0xAE00, L' ', 0xACBD, 0xB85C, 0 };
	const std::string Utf8 = StringConv::WideToUtf8(Wide);
	CHECK(Utf8 == "\xED\x95\x9C\xEA\xB8\x80 \xEA\xB2\xBD\xEB\xA1\x9C");
	CHECK(StringConv::Utf8ToWide(Utf8) == std::wstring(Wide));
	CHECK(StringConv::WideToUtf8(L"").empty());
	// U+1F600 (a surrogate pair) round-trips too.
	const wchar_t Emoji[] = { 0xD83D, 0xDE00, 0 };
	CHECK(StringConv::WideToUtf8(Emoji) == "\xF0\x9F\x98\x80");
	CHECK(StringConv::PathToUtf8(StringConv::Utf8ToPath(Utf8)) == Utf8);

	// wmain argv -> UTF-8 argv -> FCommandLine (programs enter through wmain, docs/Progress.md M1).
	wchar_t Arg0[] = L"PhysicsTestBench.exe";
	wchar_t Arg1[] = { L'-', L'O', L'u', L't', L'=', L'D', L':', L'\\', 0xD55C, 0xAE00, L' ', 0xACBD, 0xB85C, 0 };
	wchar_t* ArgVW[] = { Arg0, Arg1 };
	const FUtf8ArgV Args(2, ArgVW);
	REQUIRE(Args.Num() == 2);
	CHECK(Args.Get()[2] == nullptr);
	const std::string Built = FCommandLine::BuildFromArgV(nullptr, Args.Num(), Args.Get(), nullptr);
	CHECK(Built == "-Out=\"D:\\" + Utf8 + "\"");
	std::string Out;
	CHECK(FParse::Value(Built.c_str(), "Out=", Out));
	CHECK(Out == "D:\\" + Utf8);
}

TEST_CASE("PhysicsTest.Core.CommandLineUtf8Probe")
{
	// CTest PhysicsTest.Unit.CommandLineUtf8 starts the exe with -PtUtf8Probe="한글 경로" and runs this case: the
	// value must arrive as UTF-8 through wmain (main(int, char**) would see the ANSI code page, cp949).
	std::string Probe;
	if (!FParse::Value(FCommandLine::Get(), "PtUtf8Probe=", Probe))
	{
		MESSAGE("no -PtUtf8Probe= argument (only the dedicated CTest entry passes one)");
		return;
	}
	CHECK(Probe == "\xED\x95\x9C\xEA\xB8\x80 \xEA\xB2\xBD\xEB\xA1\x9C");
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

TEST_CASE("PhysicsTest.Core.RandomStreamGoldenValues")
{
	// Scene generation and desc_sha256 depend on this exact sequence. The expected values come from an independent
	// MT19937-64 implementation (Matsumoto & Nishimura's reference algorithm, checked against the standard's
	// 10000th-output requirement 9981545732273789042), the harness [0,1) mappings (53 bits: (x >> 11) * 2^-53;
	// 24 bits: (x >> 40) * 2^-24) and the seeding path int32 -> uint32 -> uint64.
	{
		std::mt19937_64 Reference;   // the standard's own conformance value (default seed 5489)
		Reference.discard(9999);
		CHECK(Reference() == 9981545732273789042ull);
	}
	{
		FRandomStream S(FRandomStream::DefaultSeed);   // 0x5EED
		CHECK(S.GetFraction() == 0x1.c271be2ef4ba8p-1);
		CHECK(S.GetFraction() == 0x1.4b044f345c238p-2);
		CHECK(S.GetFraction() == 0x1.d2fdbca73c8dcp-3);
		CHECK(S.FRand() == 0x1.13006p-1f);
		CHECK(S.FRand() == 0x1.01ec6p-1f);
		CHECK(S.FRand() == 0x1.823652p-1f);
		S.Reset();
		CHECK(S.GetUnsignedInt() == 3778600727u);
		CHECK(S.GetUnsignedInt() == 1388385229u);
		CHECK(S.GetUnsignedInt() == 979351444u);
		S.Reset();
		const int32 Expected[] = { 3, -1, -2, 0, 0, 2, -1, 0 };
		for (const int32 Value : Expected)
		{
			CHECK(S.RandRange(-3, 3) == Value);
		}
	}
	{
		// A negative seed goes through uint32: -1 -> 0xFFFFFFFF -> mt19937_64 seed 4294967295.
		FRandomStream S(-1);
		CHECK(S.GetInitialSeed() == -1);
		CHECK(S.GetFraction() == 0x1.a7759e8bbab5cp-3);
		CHECK(S.GetFraction() == 0x1.a9b006768695cp-1);
		CHECK(S.GetFraction() == 0x1.aa5712f6fb4dcp-1);
		CHECK(S.FRand() == 0x1.028d7ap-1f);
		CHECK(S.FRand() == 0x1.22c25p-2f);
		CHECK(S.FRand() == 0x1.6f88bp-2f);
		S.Reset();
		const int32 Expected[] = { -2, 2, 2, 0, -2, -1, 0, -3 };
		for (const int32 Value : Expected)
		{
			CHECK(S.RandRange(-3, 3) == Value);
		}
	}
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
