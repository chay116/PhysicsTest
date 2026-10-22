// ThreadGate.cpp - G10b (see Gates/ThreadGate.h).
#include "Gates/ThreadGate.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

namespace PhysicsTest::Gates
{
	namespace
	{
		std::string Format(const char* Pattern, const double A, const double B = 0.0, const double C = 0.0, const double D = 0.0)
		{
			char Text[320];
			std::snprintf(Text, sizeof(Text), Pattern, A, B, C, D);
			return Text;
		}

		FGateReport Skip(FGateReport R, const EG10bSkip Why, EG10bSkip* OutSkip, const std::string& Note)
		{
			R.Result = EGateResult::Skip;
			R.Note(Note);
			if (OutSkip != nullptr)
			{
				*OutSkip = Why;
			}
			return R;
		}
	}

	const char* ToString(const EG10bSkip Skip)
	{
		switch (Skip)
		{
		case EG10bSkip::None: return "";
		case EG10bSkip::MultiThread: return "multi_thread";
		case EG10bSkip::NoCensus: return "no_census";
		case EG10bSkip::ShortWindow: return "short_window";
		case EG10bSkip::Contended: return "contended";
		}
		return "?";
	}

	FGateReport EvaluateG10b(const FCensusWindow& Window, const std::string& Label, EG10bSkip* OutSkip)
	{
		FGateReport R;
		R.Gate = "G10b";
		R.Target = Label;
		if (OutSkip != nullptr)
		{
			*OutSkip = EG10bSkip::None;
		}
		if (!Window.bValid)
		{
			return Skip(std::move(R), EG10bSkip::NoCensus, OutSkip, "no census window: " + Window.Error);
		}
		const double OwnSeconds = Window.GetOwnSeconds();
		const double OtherFraction = Window.GetOtherOnMaskFraction();
		R.Note(Format("cpu_eff %.5f (own cycles / (wall x T - other on the mask)), cpu_eff_raw %.5f (own / (wall x T)), foreign %.4f %%", Window.CpuEff,
			Window.CpuEffRaw, 100.0 * Window.ForeignFraction));
		R.Note(Format("window %.3f ms wall, %.3f ms own CPU time, T %.0f, other processes / interrupts on the mask %.3f %% of wall x T", Window.WallSeconds * 1.0e3,
			OwnSeconds * 1.0e3, static_cast<double>(Window.Threads), 100.0 * OtherFraction));
		R.Note(std::to_string(Window.ThreadsBegin) + " thread(s) at census #1, " + std::to_string(Window.ThreadsEnd) + " at census #2");
		const double MsPerCycle = Window.WallTsc > 0 ? 1.0e3 * Window.WallSeconds / static_cast<double>(Window.WallTsc) : 0.0;
		R.Note(Format("unattributed (process - own: a thread in neither census, born and gone inside the window) %.4f %% of own after the reads' baseline; "
			"raw %.3f ms, baseline %.3f ms", 100.0 * Window.UnattributedFraction, MsPerCycle * static_cast<double>(Window.UnattributedCycles),
			MsPerCycle * static_cast<double>(Window.BracketCycles)));
		for (const FCensusThreadDelta& Thread : Window.PerThread)
		{
			if (!Thread.bMain && Thread.Cycles > 0)
			{
				char Text[256];
				std::snprintf(Text, sizeof(Text), "thread %u%s%s%s: %" PRIu64 " cycles (%.4f %% of own)", Thread.ThreadId, Thread.Name.empty() ? "" : " '",
					Thread.Name.c_str(), Thread.Name.empty() ? "" : "'", Thread.Cycles,
					Window.OwnCycles > 0 ? 100.0 * static_cast<double>(Thread.Cycles) / static_cast<double>(Window.OwnCycles) : 0.0);
				R.Note(Text);
			}
		}
		if (Window.Threads != 1)
		{
			return Skip(std::move(R), EG10bSkip::MultiThread, OutSkip,
				"T = " + std::to_string(Window.Threads) + ": G10b judges T = 1 runs only (the values are recorded for the scaling analysis)");
		}
		if (OwnSeconds < G10bMinOwnSeconds)
		{
			return Skip(std::move(R), EG10bSkip::ShortWindow, OutSkip, Format("own CPU time %.3f ms (wall %.3f ms) below %.0f ms: too few cycles for a verdict "
				"(the stamp's G10b run judges the backend)", OwnSeconds * 1.0e3, Window.WallSeconds * 1.0e3, G10bMinOwnSeconds * 1.0e3));
		}
		const bool bCpuEff = Window.CpuEff >= G10bCpuEffMin && Window.CpuEff <= G10bCpuEffMax;
		// M10 fix round 0: the cycles of a thread in neither census (unattributed) are another thread's work too.
		const double ExtraOfOwn = Window.ForeignFraction + Window.UnattributedFraction;
		const bool bExtra = ExtraOfOwn < G10bForeignMax;
		// M10 fix round 1: in a contended window, the same extra cycles against the mask's busy cycles (own + other processes).
		// OtherOnMask is MaskBusy - own, so a thread of this process in neither census (unattributed) counts there too: when it
		// alone makes a window "contended" (> 5 % of the mask), its cycles are also in ExtraCycles and far above 0.5 % of the busy
		// cycles, so the run still fails (the G10b negative control does: unattributed ~6 % of own, other 8-11 %).
		const double ExtraCycles = ExtraOfOwn * static_cast<double>(Window.OwnCycles);
		const double BusyCycles = static_cast<double>(std::max(Window.MaskBusyCycles, Window.OwnCycles));
		const double ExtraOfBusy = BusyCycles > 0.0 ? ExtraCycles / BusyCycles : 0.0;
		const bool bContended = OtherFraction > G10bContendedOtherMax;
		if (bCpuEff && !bExtra && bContended && ExtraOfBusy < G10bForeignMax)
		{
			return Skip(std::move(R), EG10bSkip::Contended, OutSkip, Format("contended window (other processes took %.2f %% of the mask, > %.0f %%): foreign + "
				"unattributed %.4f %% of own but %.4f %% of the mask's busy cycles - a thread that runs in proportion to wall time and the reads' residue grow "
				"against own on a starved core, so this is no verdict", 100.0 * OtherFraction, 100.0 * G10bContendedOtherMax, 100.0 * ExtraOfOwn, 100.0 * ExtraOfBusy));
		}
		R.Check(bCpuEff, Format("cpu_eff %.5f outside [%.2f, %.2f] (the process left its core idle or overbooked it)", Window.CpuEff, G10bCpuEffMin, G10bCpuEffMax));
		R.Check(bExtra, Format("foreign thread cycles %.4f %% + unattributed %.4f %% of the process's >= %.2f %%: a thread other than the timing thread worked at T = 1",
			100.0 * Window.ForeignFraction, 100.0 * Window.UnattributedFraction, 100.0 * G10bForeignMax) + (bContended
			? Format(" (contended window: also %.4f %% of the mask's busy cycles)", 100.0 * ExtraOfBusy) : std::string()));
		return R;
	}
}
