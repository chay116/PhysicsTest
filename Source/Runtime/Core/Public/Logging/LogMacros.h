// LogMacros.h - UE_LOG and log categories (UE: Core/Public/Logging/LogMacros.h, LogVerbosity.h), minimal (DL18).
//
//   DECLARE_LOG_CATEGORY_EXTERN(LogPhysicsTest, Log, All);   // header
//   DEFINE_LOG_CATEGORY(LogPhysicsTest);                      // one .cpp
//   UE_LOG(LogPhysicsTest, Warning, "Substeps clamped to %d", NumSteps);
//
// Differences from UE: format strings are plain UTF-8 char strings (no TEXT()/TCHAR), output goes to stdout
// (Display/Log/Verbose) or stderr (Fatal/Error/Warning) as "Category: Verbosity: message", plus OutputDebugString
// when a debugger is attached. Fatal flushes and terminates the process (exit code 3).
#pragma once

#include "CoreTypes.h"

#include <type_traits>

namespace PhysicsTest
{
	enum class ELogVerbosity : uint8
	{
		NoLogging = 0,
		Fatal,
		Error,
		Warning,
		Display,
		Log,
		Verbose,
		VeryVerbose,
		All = VeryVerbose,
	};

	[[nodiscard]] const char* ToString(ELogVerbosity Verbosity);

	class FLogCategoryBase
	{
	public:
		FLogCategoryBase(const char* InCategoryName, ELogVerbosity InDefaultVerbosity)
			: CategoryName(InCategoryName), Verbosity(InDefaultVerbosity)
		{
		}

		[[nodiscard]] const char* GetCategoryName() const { return CategoryName; }
		[[nodiscard]] ELogVerbosity GetVerbosity() const { return Verbosity; }
		void SetVerbosity(const ELogVerbosity InVerbosity) { Verbosity = InVerbosity; }
		// Fatal is always active, as in UE.
		[[nodiscard]] bool IsSuppressed(const ELogVerbosity InVerbosity) const
		{
			return InVerbosity != ELogVerbosity::Fatal && InVerbosity > Verbosity;
		}

	private:
		const char* CategoryName;
		ELogVerbosity Verbosity;
	};

	template <ELogVerbosity InDefaultVerbosity, ELogVerbosity InCompileTimeVerbosity>
	class TLogCategory : public FLogCategoryBase
	{
	public:
		static constexpr ELogVerbosity CompileTimeVerbosity = InCompileTimeVerbosity;

		explicit TLogCategory(const char* InCategoryName) : FLogCategoryBase(InCategoryName, InDefaultVerbosity) {}
	};

	namespace Logging
	{
		// Formats and writes one line. Fatal never returns.
		void Write(const FLogCategoryBase& Category, ELogVerbosity Verbosity, const char* File, int32 Line, const char* Format, ...);
		// M6: a program whose stdout carries data (PhysicsTestBench -Describe / -List / -SceneHashes: pure JSON for the M7 / M8
		// tools) sends every log line to stderr from here on. By default Warning and worse go to stderr, the rest to stdout.
		void SetAllLinesToStderr(bool bAllToStderr);
	}
}

#define DECLARE_LOG_CATEGORY_EXTERN(CategoryName, DefaultVerbosity, CompileTimeVerbosity) \
	extern struct FLogCategory##CategoryName \
		: public ::PhysicsTest::TLogCategory<::PhysicsTest::ELogVerbosity::DefaultVerbosity, ::PhysicsTest::ELogVerbosity::CompileTimeVerbosity> \
	{ \
		FLogCategory##CategoryName() : TLogCategory(#CategoryName) {} \
	} CategoryName

#define DEFINE_LOG_CATEGORY(CategoryName) FLogCategory##CategoryName CategoryName

#define DEFINE_LOG_CATEGORY_STATIC(CategoryName, DefaultVerbosity, CompileTimeVerbosity) \
	static struct FLogCategory##CategoryName \
		: public ::PhysicsTest::TLogCategory<::PhysicsTest::ELogVerbosity::DefaultVerbosity, ::PhysicsTest::ELogVerbosity::CompileTimeVerbosity> \
	{ \
		FLogCategory##CategoryName() : TLogCategory(#CategoryName) {} \
	} CategoryName

// Messages above the category's compile-time verbosity are compiled out (UE semantics).
#define UE_LOG(CategoryName, Verbosity, Format, ...) \
	do \
	{ \
		constexpr ::PhysicsTest::ELogVerbosity PtLogVerbosity_ = ::PhysicsTest::ELogVerbosity::Verbosity; \
		if constexpr (PtLogVerbosity_ <= std::remove_cvref_t<decltype(CategoryName)>::CompileTimeVerbosity) \
		{ \
			if (!(CategoryName).IsSuppressed(PtLogVerbosity_)) \
			{ \
				::PhysicsTest::Logging::Write(CategoryName, PtLogVerbosity_, __FILE__, __LINE__, Format __VA_OPT__(,) __VA_ARGS__); \
			} \
		} \
	} while (0)

DECLARE_LOG_CATEGORY_EXTERN(LogTemp, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogCore, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogPhysicsTest, Log, All);
