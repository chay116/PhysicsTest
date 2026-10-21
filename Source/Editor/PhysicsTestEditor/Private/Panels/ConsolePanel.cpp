// ConsolePanel.cpp - the Console panel: the command line (console variables, stat, play controls) and a live view of
// the console variables matching a filter (booleans as checkboxes).
#include "PanelCommon.h"

#include "Physics/PhysicsConsoleVariables.h"

#include <algorithm>
#include <cstring>

namespace PhysicsTest
{
	void FPhysicsTestEditor::DrawConsolePanel(FEditorContext& Context)
	{
		const float Width = FMath::Max(300.0f, static_cast<float>(Context.ViewWidth) - 8.0f - 340.0f - 8.0f - 490.0f - 16.0f);
		ImGui::SetNextWindowPos(ImVec2(356.0f, static_cast<float>(Context.ViewHeight) - 230.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(Width, 222.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Console", &bShowConsole))
		{
			ImGui::End();
			return;
		}
		if (ImGui::BeginTabBar("##console"))
		{
			if (ImGui::BeginTabItem("Log"))
			{
				const float Footer = ImGui::GetFrameHeightWithSpacing();
				if (ImGui::BeginChild("##log", ImVec2(0.0f, -Footer), ImGuiChildFlags_Borders))
				{
					for (const std::string& Line : Log)
					{
						ImGui::TextUnformatted(Line.c_str());
					}
					if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
					{
						ImGui::SetScrollHereY(1.0f);
					}
				}
				ImGui::EndChild();
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputTextWithHint("##input", "p.Chaos.Solver.DebugDrawCollisions 1  |  stat ChaosCounters  |  help", ConsoleInput, sizeof(ConsoleInput),
						ImGuiInputTextFlags_EnterReturnsTrue))
				{
					ExecuteConsoleCommand(ConsoleInput, Context.Session);
					ConsoleInput[0] = '\0';
					ImGui::SetKeyboardFocusHere(-1);
				}
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Variables"))
			{
				ImGui::SetNextItemWidth(240.0f);
				ImGui::InputTextWithHint("##cvarfilter", "filter", CVarFilter, sizeof(CVarFilter));
				std::string Filter = CVarFilter;
				std::transform(Filter.begin(), Filter.end(), Filter.begin(), [](const char C) { return static_cast<char>(C >= 'A' && C <= 'Z' ? C - 'A' + 'a' : C); });
				if (ImGui::BeginChild("##cvars", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
				{
					IConsoleManager::Get().ForEachConsoleVariable([&](IConsoleVariable& Variable)
					{
						std::string Lower = Variable.GetName();
						std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](const char C) { return static_cast<char>(C >= 'A' && C <= 'Z' ? C - 'A' + 'a' : C); });
						if (!Filter.empty() && Lower.find(Filter) == std::string::npos)
						{
							return;
						}
						ImGui::PushID(Variable.GetName());
						// Harness physics CVars are shown, not edited (ExecuteConsoleCommand says why).
						const bool bHarnessPhysics = PhysicsCVars::FindHarnessConsoleVariable(Variable.GetName()) != nullptr;
						if (Variable.GetType() == EConsoleVariableType::Bool && !bHarnessPhysics)
						{
							bool bValue = Variable.GetBool();
							if (ImGui::Checkbox(Variable.GetName(), &bValue))
							{
								Variable.Set(bValue ? "1" : "0", ECVF_SetByConsole);
							}
						}
						else
						{
							ImGui::Text("%s = %s", Variable.GetName(), Variable.GetString().c_str());
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("%s\n(set by %s)%s", Variable.GetHelp(), GetConsoleVariableSetByName(Variable.GetSetBy()),
								bHarnessPhysics ? "\nharness physics CVar: read-only here (profile, -ExecCmds= or -ini:)" : "");
						}
						ImGui::PopID();
					});
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::End();
	}
}
