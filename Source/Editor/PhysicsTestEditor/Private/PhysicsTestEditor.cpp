// PhysicsTestEditor.cpp - FPhysicsTestEditor: menu, dockspace, console commands, requests (panels: Panels/*.cpp).
#include "Panels/PanelCommon.h"

#include "Physics/PhysicsConsoleVariables.h"
#include "SceneView.h"

#include <algorithm>

namespace PhysicsTest
{
	namespace
	{
		// The Show menu: the Chaos switches (p.Chaos.Solver.DebugDraw*), in PostTickDebugDraw order.
		struct FShowFlag
		{
			const char* Label;
			const char* Variable;
		};
		const FShowFlag GShowFlags[] = {
			{ "Shapes", "p.Chaos.Solver.DebugDrawShapes" },
			{ "Collisions", "p.Chaos.Solver.DebugDrawCollisions" },
			{ "Mass", "p.Chaos.Solver.DebugDrawMass" },
			{ "Bounds", "p.Chaos.Solver.DebugDrawBounds" },
			{ "Transforms", "p.Chaos.Solver.DebugDrawTransforms" },
			{ "Islands (constraint graph)", "p.Chaos.Solver.DebugDrawIslands" },
			{ "Sleep state", "p.Chaos.Solver.DebugDrawSleepState" },
			{ "Colliding shapes", "p.Chaos.Solver.DebugDrawCollidingShapes" },
			{ "Joints", "p.Chaos.Solver.DebugDrawJoints" },
		};
		const FShowFlag GShowOptions[] = {
			{ "Debug draw enabled", "p.Chaos.DebugDraw.Enabled" },
			{ "See through", "p.Chaos.DebugDraw.SeeThrough" },
			{ "Inactive contacts", "p.Chaos.DebugDraw.ShowInactiveContacts" },
			{ "Statics", "p.Chaos.Solver.DebugDraw.ShowStatics" },
			{ "Kinematics", "p.Chaos.Solver.DebugDraw.ShowKinematics" },
			{ "Dynamics", "p.Chaos.Solver.DebugDraw.ShowDynamics" },
		};

		void ToggleMenuItem(const FShowFlag& Flag)
		{
			IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Flag.Variable);
			if (Variable == nullptr)
			{
				return;
			}
			const bool bValue = Variable->GetBool();
			if (ImGui::MenuItem(Flag.Label, nullptr, bValue))
			{
				Variable->Set(bValue ? "0" : "1", ECVF_SetByConsole);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", Flag.Variable);
			}
		}
	}

	FPhysicsTestEditor::FPhysicsTestEditor()
	{
		AddLog("PhysicsTestViewer - type 'help' for commands; every time shown is viz mode, not a benchmark.");
	}

	void FPhysicsTestEditor::Draw(FEditorContext& Context)
	{
		if (!bPendingInitialised && Context.Session.HasScene())
		{
			Pending = Context.Session.GetSelection();
			bPendingInitialised = true;
		}
		ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
		DrawMainMenu(Context);
		if (bShowEngines)
		{
			DrawEnginesPanel(Context);
		}
		if (bShowInspector)
		{
			DrawInspectorPanel(Context);
		}
		if (bShowStats)
		{
			DrawStatsPanel(Context);
		}
		if (bShowConsole)
		{
			DrawConsolePanel(Context);
		}
		if (bShowDemo)
		{
			ImGui::ShowDemoWindow(&bShowDemo);
		}
		// Everything culled by Chaos' region / budget: say so on the viewport instead of showing an empty scene.
		const FDebugDrawQueueStats& Queue = Context.Viewport.GetQueueStats();
		const FDebugDrawRenderStats& Render = Context.Viewport.GetRenderStageStats();
		const int32 RejectedRegion = Queue.NumRejectedRegion + Render.NumRejectedRegion;
		const int32 RejectedBudget = Queue.NumRejectedBudget + Render.NumRejectedBudget;
		if (Context.Session.HasScene() && Render.NumRendered == 0 && Render.NumLatent == 0 && (RejectedRegion > 0 || RejectedBudget > 0))
		{
			char Text[256];
			std::snprintf(Text, sizeof(Text), "Nothing drawn: %d outside p.Chaos.DebugDraw.Radius %.0f cm of the camera, %d over p.Chaos.DebugDraw.MaxLines.  "
				"Show > Draw everything", RejectedRegion, Context.Viewport.GetFlags().Radius, RejectedBudget);
			const ImVec2 Size = ImGui::CalcTextSize(Text);
			const ImVec2 Position((static_cast<float>(Context.ViewWidth) - Size.x) * 0.5f, static_cast<float>(Context.ViewHeight) * 0.5f);
			ImGui::GetBackgroundDrawList()->AddText(Position, IM_COL32(255, 200, 80, 255), Text);
		}
	}

	void FPhysicsTestEditor::DrawMainMenu(FEditorContext& Context)
	{
		if (!ImGui::BeginMainMenuBar())
		{
			return;
		}
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Screenshot", "F9"))
			{
				bScreenshotRequested = true;
			}
			if (ImGui::MenuItem("Reset scene", "Ctrl+R"))
			{
				bResetRequested = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit"))
			{
				bExitRequested = true;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem("Engines", nullptr, &bShowEngines);
			ImGui::MenuItem("Inspector", nullptr, &bShowInspector);
			ImGui::MenuItem("Stats (stat ChaosCounters)", nullptr, &bShowStats);
			ImGui::MenuItem("Console", nullptr, &bShowConsole);
			ImGui::Separator();
			if (ImGui::MenuItem("Frame selection", "F"))
			{
				bFocusRequested = true;
			}
			ImGui::MenuItem("ImGui demo", nullptr, &bShowDemo);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Show"))
		{
			for (const FShowFlag& Flag : GShowFlags)
			{
				ToggleMenuItem(Flag);
			}
			ImGui::Separator();
			for (const FShowFlag& Flag : GShowOptions)
			{
				ToggleMenuItem(Flag);
			}
			ImGui::Separator();
			// Chaos' defaults draw at most 20000 lines within 3000 cm of the camera (p.Chaos.DebugDraw.MaxLines / .Radius); a
			// large scene viewed from afar shows nothing until these are lifted.
			if (ImGui::MenuItem("Draw everything (MaxLines 0, Radius 0)"))
			{
				ExecuteConsoleCommand("p.Chaos.DebugDraw.MaxLines 0", Context.Session);
				ExecuteConsoleCommand("p.Chaos.DebugDraw.Radius 0", Context.Session);
			}
			if (ImGui::MenuItem("Chaos budget (MaxLines 20000, Radius 3000)"))
			{
				ExecuteConsoleCommand("p.Chaos.DebugDraw.MaxLines 20000", Context.Session);
				ExecuteConsoleCommand("p.Chaos.DebugDraw.Radius 3000", Context.Session);
			}
			ImGui::EndMenu();
		}
		// Status on the right: the scene, the frame, the play state.
		const FViewerSession& Session = Context.Session;
		if (Session.HasScene())
		{
			ImGui::Separator();
			ImGui::Text("%s  |  %s  %s/%s  |  frame %llu%s", Session.GetScenario()->GetName(), Session.GetSelection().Backend.c_str(),
				Session.GetConfig().ProfileName.c_str(), Session.GetConfigFiles().VariantName.empty() ? "-" : Session.GetConfigFiles().VariantName.c_str(),
				static_cast<unsigned long long>(Session.GetFrameCount()), Session.IsPaused() ? "  (paused)" : "");
		}
		ImGui::EndMainMenuBar();
	}

	void FPhysicsTestEditor::DrawWorldStrings(const std::vector<FDebugDrawString>& Strings, const FSceneView& View)
	{
		if (Strings.empty())
		{
			return;
		}
		ImDrawList* DrawList = ImGui::GetBackgroundDrawList();
		ImFont* Font = ImGui::GetFont();
		const float BaseSize = ImGui::GetFontSize();
		for (const FDebugDrawString& String : Strings)
		{
			double X = 0.0;
			double Y = 0.0;
			if (!View.ProjectWorldToPixel(String.Location, X, Y))
			{
				continue;
			}
			const float Size = BaseSize * (String.FontScale > 0.0f ? String.FontScale : 1.0f);
			const ImVec2 Position(static_cast<float>(X), static_cast<float>(Y));
			if (String.bDrawShadow)
			{
				DrawList->AddText(Font, Size, ImVec2(Position.x + 1.0f, Position.y + 1.0f), IM_COL32(0, 0, 0, 255), String.Text.c_str());
			}
			DrawList->AddText(Font, Size, Position, IM_COL32(String.Color.R, String.Color.G, String.Color.B, 255), String.Text.c_str());
		}
	}

	void FPhysicsTestEditor::SetOpenResult(const EViewerOpenResult Result, const std::string& Message, const FViewerSelection& Requested)
	{
		LastOpenResult = Result;
		LastOpenMessage = Message;
		LastRequested = Requested;
		if (Result == EViewerOpenResult::Opened)
		{
			Pending = Requested;
			bPendingInitialised = true;
		}
		AddLog(std::string("open ") + Requested.Backend + " / " + Requested.Scenario + ": " + ToString(Result) + (Message.empty() ? "" : " - " + Message));
	}

	void FPhysicsTestEditor::AddLog(const std::string& Line)
	{
		Log.push_back(Line);
		if (Log.size() > 500)
		{
			Log.erase(Log.begin(), Log.begin() + 100);
		}
	}

	bool FPhysicsTestEditor::ExecuteConsoleCommand(const std::string& InLine, FViewerSession& Session)
	{
		std::string Line = InLine;
		while (!Line.empty() && (Line.back() == ' ' || Line.back() == '\t'))
		{
			Line.pop_back();
		}
		while (!Line.empty() && (Line.front() == ' ' || Line.front() == '\t'))
		{
			Line.erase(Line.begin());
		}
		if (Line.empty())
		{
			return true;
		}
		AddLog("> " + Line);
		History.push_back(Line);
		std::string Lower = Line;
		std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](const char C) { return static_cast<char>(C >= 'A' && C <= 'Z' ? C - 'A' + 'a' : C); });
		if (Lower == "help")
		{
			AddLog("  <cvar> [value]        show or set a console variable (p.Chaos.DebugDraw.*, p.Chaos.Solver.DebugDraw*)");
			AddLog("                        harness physics CVars (p.ComNudgeAffectsInertia, ...) are read-only here: set them in");
			AddLog("                        the profile or with -ExecCmds= / -ini: on the command line");
			AddLog("  stat ChaosCounters    toggle the Stats panel");
			AddLog("  pause | play | step | reset | screenshot | frame");
			AddLog("  open <Backend> [<Variant>|-]  this selection on another backend / variant (as Apply; - = default)");
			AddLog("  restart               relaunch with the last selection that needed a restart (as the Engines button)");
			AddLog("  cvars [filter]        list console variables");
			return true;
		}
		if (Lower == "open" || Lower.rfind("open ", 0) == 0)
		{
			// open <Backend> [<Variant>|-]: the selection of the running scene (else the Engines panel's) with another backend /
			// variant; ProcessRequests opens it like Apply (RestartRequired included).
			std::vector<std::string> Words;
			size_t Cursor = 4;
			while (Cursor < Line.size())
			{
				const size_t Begin = Line.find_first_not_of(" \t", Cursor);
				if (Begin == std::string::npos)
				{
					break;
				}
				const size_t End = Line.find_first_of(" \t", Begin);
				Words.push_back(Line.substr(Begin, End == std::string::npos ? std::string::npos : End - Begin));
				Cursor = End == std::string::npos ? Line.size() : End;
			}
			if (Words.empty() || Words.size() > 2)
			{
				AddLog("  usage: open <Backend> [<Variant>|-]");
				return false;
			}
			FViewerSelection Wanted = Session.HasScene() ? Session.GetSelection() : Pending;
			Wanted.Backend = Words[0];
			Wanted.BackendProfile = Words.size() > 1 && Words[1] != "-" ? Words[1] : std::string();
			RequestedOpen = Wanted;
			bOpenRequested = true;
			bConfigListsStale = true;
			return true;
		}
		if (Lower == "restart")
		{
			if (LastOpenResult != EViewerOpenResult::RestartRequired)
			{
				AddLog("  nothing to restart: the last open was " + std::string(ToString(LastOpenResult)));
				return false;
			}
			RequestRestart();
			return true;
		}
		if (Lower == "stat chaoscounters")
		{
			bShowStats = !bShowStats;
			return true;
		}
		if (Lower == "pause" || Lower == "play")
		{
			Session.SetPaused(Lower == "pause");
			return true;
		}
		if (Lower == "step")
		{
			Session.SetPaused(true);
			Session.RequestSingleStep();
			return true;
		}
		if (Lower == "reset")
		{
			bResetRequested = true;
			return true;
		}
		if (Lower == "screenshot")
		{
			bScreenshotRequested = true;
			return true;
		}
		if (Lower == "frame")
		{
			bFocusRequested = true;
			return true;
		}
		if (Lower.rfind("cvars", 0) == 0)
		{
			const std::string Filter = Lower.size() > 6 ? Lower.substr(6) : std::string();
			IConsoleManager::Get().ForEachConsoleVariable([&](IConsoleVariable& Variable)
			{
				std::string Name = Variable.GetName();
				std::string LowerName = Name;
				std::transform(LowerName.begin(), LowerName.end(), LowerName.begin(), [](const char C) { return static_cast<char>(C >= 'A' && C <= 'Z' ? C - 'A' + 'a' : C); });
				if (Filter.empty() || LowerName.find(Filter) != std::string::npos)
				{
					AddLog("  " + Name + " = " + Variable.GetString());
				}
			});
			return true;
		}
		// A harness physics CVar belongs to the scene's resolved config: FPhysScene treats a change under a live scene as fatal
		// at its next SpawnBody / FlushDeferredActors (VerifyConsoleVariables), and Apply / Reset re-resolves it from the config
		// anyway. So the console shows it but does not set it.
		const size_t NameEnd = Line.find_first_of(" \t=");
		if (NameEnd != std::string::npos && PhysicsCVars::FindHarnessConsoleVariable(std::string_view(Line).substr(0, NameEnd)) != nullptr
			&& Line.find_first_not_of(" \t=", NameEnd) != std::string::npos)
		{
			AddLog("  " + Line.substr(0, NameEnd) + " is a harness physics CVar: set it in the profile or with -ExecCmds= / -ini: on the command line "
				"(Apply / Reset resolves it for the next scene); it is not changed under a live scene");
			return false;
		}
		std::string Message;
		const bool bOk = IConsoleManager::Get().ProcessUserConsoleInput(Line, ECVF_SetByConsole, &Message);
		AddLog(Message.empty() ? (bOk ? std::string("  ok") : std::string("  unknown command or variable")) : "  " + Message);
		return bOk;
	}

	bool FPhysicsTestEditor::ConsumeOpenRequest(FViewerSelection& OutSelection)
	{
		if (!bOpenRequested)
		{
			return false;
		}
		bOpenRequested = false;
		OutSelection = RequestedOpen;
		return true;
	}

	void FPhysicsTestEditor::RequestRestart()
	{
		// The new selection plus this process's session switches (-ini:, -ExecCmds, -ConfigDir, -ResX/-ResY, -NoVSync, ...;
		// a -Headless process also keeps its run control, GetForwardedSwitches).
		RestartCommandLine = FViewerSession::MakeCommandLine(LastRequested, FViewerSession::GetForwardedSwitches(FCommandLine::Get()));
		bRestartRequested = true;
	}

	bool FPhysicsTestEditor::ConsumeRestartRequest(std::string& OutCommandLine)
	{
		if (!bRestartRequested)
		{
			return false;
		}
		bRestartRequested = false;
		OutCommandLine = RestartCommandLine;
		return true;
	}
}
