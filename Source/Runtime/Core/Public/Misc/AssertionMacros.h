// AssertionMacros.h - check / checkf / verify / verifyf / ensure / ensureMsgf / checkNoEntry
// (UE: Core/Public/Misc/AssertionMacros.h), minimal (DL18).
//
//   PT_DO_CHECK=1 (dbg, dev): check*/verify* log a Fatal message and terminate (exit code 3, debugger break first);
//                             ensure* logs an Error once per call site and evaluates to false.
//   PT_DO_CHECK=0 (test):     check* expressions are not evaluated, verify* expressions are evaluated without a test,
//                             ensure*(x) is (!!(x)) - the same split as UE's DO_CHECK.
//
// Third-party headers that declare members named check / verify / ensure (doctest: doctest.h:1647) must be
// included between Misc/PtThirdPartyIncludesStart.h and Misc/PtThirdPartyIncludesEnd.h (DL03).
#pragma once

#include "CoreTypes.h"

namespace PhysicsTest::Assert
{
	[[noreturn]] void OnCheckFailed(const char* Expr, const char* File, int32 Line);
	[[noreturn]] void OnCheckFailedf(const char* Expr, const char* File, int32 Line, const char* Format, ...);
	// Logs (first failure per call site only) and returns false.
	bool OnEnsureFailed(bool& bExecuted, const char* Expr, const char* File, int32 Line);
	bool OnEnsureFailedf(bool& bExecuted, const char* Expr, const char* File, int32 Line, const char* Format, ...);
}

#if PT_DO_CHECK
	#define check(Expr) \
		do \
		{ \
			if (!(Expr)) [[unlikely]] \
			{ \
				::PhysicsTest::Assert::OnCheckFailed(#Expr, __FILE__, __LINE__); \
			} \
		} while (0)
	#define checkf(Expr, Format, ...) \
		do \
		{ \
			if (!(Expr)) [[unlikely]] \
			{ \
				::PhysicsTest::Assert::OnCheckFailedf(#Expr, __FILE__, __LINE__, Format __VA_OPT__(,) __VA_ARGS__); \
			} \
		} while (0)
	#define verify(Expr) check(Expr)
	#define verifyf(Expr, Format, ...) checkf(Expr, Format __VA_OPT__(,) __VA_ARGS__)
	#define checkNoEntry() ::PhysicsTest::Assert::OnCheckFailed("Enclosing block should never be called", __FILE__, __LINE__)
	#define ensure(Expr) \
		((!!(Expr)) || [](const char* PtExpr_, const char* PtFile_, int PtLine_) \
		{ \
			static bool bPtExecuted_ = false; \
			return ::PhysicsTest::Assert::OnEnsureFailed(bPtExecuted_, PtExpr_, PtFile_, PtLine_); \
		}(#Expr, __FILE__, __LINE__))
	#define ensureMsgf(Expr, Format, ...) \
		((!!(Expr)) || [&](const char* PtExpr_, const char* PtFile_, int PtLine_) \
		{ \
			static bool bPtExecuted_ = false; \
			return ::PhysicsTest::Assert::OnEnsureFailedf(bPtExecuted_, PtExpr_, PtFile_, PtLine_, Format __VA_OPT__(,) __VA_ARGS__); \
		}(#Expr, __FILE__, __LINE__))
#else
	#define check(Expr) do { (void)sizeof(!(Expr)); } while (0)
	#define checkf(Expr, Format, ...) do { (void)sizeof(!(Expr)); } while (0)
	#define verify(Expr) do { if (!(Expr)) {} } while (0)
	#define verifyf(Expr, Format, ...) do { if (!(Expr)) {} } while (0)
	#define checkNoEntry() do {} while (0)
	#define ensure(Expr) (!!(Expr))
	#define ensureMsgf(Expr, Format, ...) (!!(Expr))
#endif
