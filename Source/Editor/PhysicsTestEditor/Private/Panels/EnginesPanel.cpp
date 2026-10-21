// EnginesPanel.cpp - the Engines panel: backend / variant / profile / scenario selection, play controls, restart prompt.
#include "PanelCommon.h"

#include <algorithm>
#include <thread>

namespace PhysicsTest
{
	namespace
	{
		bool ComboFromList(const char* Label, std::string& InOutValue, const std::vector<std::string>& Items, const char* NoneLabel = nullptr)
		{
			bool bChanged = false;
			const std::string Preview = InOutValue.empty() && NoneLabel != nullptr ? NoneLabel : InOutValue;
			if (ImGui::BeginCombo(Label, Preview.c_str()))
			{
				if (NoneLabel != nullptr && ImGui::Selectable(NoneLabel, InOutValue.empty()))
				{
					InOutValue.clear();
					bChanged = true;
				}
				for (const std::string& Item : Items)
				{
					if (ImGui::Selectable(Item.c_str(), Item == InOutValue))
					{
						InOutValue = Item;
						bChanged = true;
					}
				}
				ImGui::EndCombo();
			}
			return bChanged;
		}
	}

	void FPhysicsTestEditor::DrawEnginesPanel(FEditorContext& Context)
	{
		ImGui::SetNextWindowPos(ImVec2(8.0f, 28.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(340.0f, 430.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Engines", &bShowEngines))
		{
			ImGui::End();
			return;
		}
		FViewerSession& Session = Context.Session;

		// ---- play controls ----
		if (ImGui::Button(Session.IsPaused() ? "Play" : "Pause"))
		{
			Session.SetPaused(!Session.IsPaused());
		}
		ImGui::SameLine();
		if (ImGui::Button("Step"))
		{
			Session.SetPaused(true);
			Session.RequestSingleStep();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset"))
		{
			bResetRequested = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Screenshot"))
		{
			bScreenshotRequested = true;
		}
		ImGui::Text("FrameCount %llu   SimTime %.4f s", static_cast<unsigned long long>(Session.GetFrameCount()),
			Session.HasScene() ? Session.GetScene()->GetSimTime() : 0.0);
		ImGui::Separator();

		// ---- selection (applied with Apply) ----
		const std::vector<std::string> Backends = FPhysicsBackendRegistry::Get().GetAvailableNames();
		if (ComboFromList("Backend", Pending.Backend, Backends))
		{
			Pending.BackendProfile.clear();
		}
		// The variant and profile lists come from Config/: read again when the backend changes, after an Apply and on
		// "Refresh lists" (a file added while the viewer runs), not every frame.
		if (bConfigListsStale || ConfigListsBackend != Pending.Backend)
		{
			ConfigListsBackend = Pending.Backend;
			ConfigVariants = FViewerSession::ListBackendVariants(Pending.Backend);
			ConfigProfiles = FViewerSession::ListPhysicsProfiles();
			bConfigListsStale = false;
		}
		ComboFromList("Variant", Pending.BackendProfile, ConfigVariants, "(Default / none)");
		ComboFromList("Profile", Pending.PhysicsProfile, ConfigProfiles);
		if (ImGui::SmallButton("Refresh lists"))
		{
			bConfigListsStale = true;
		}
		std::vector<std::string> ScenarioNames;
		for (IPhysicsTestScenario* Scenario : FScenarioRegistry::Get().GetAll())
		{
			ScenarioNames.push_back(Scenario->GetName());
		}
		if (ComboFromList("Scenario", Pending.Scenario, ScenarioNames))
		{
			Pending.Params.clear();
		}
		if (const IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(Pending.Scenario))
		{
			for (const FScenarioParamDesc& Param : Scenario->GetParamDescs())
			{
				const auto Found = Pending.Params.find(Param.Name);
				std::string Value = Found != Pending.Params.end() ? Found->second : Param.Default;
				const std::string Label = "  " + Param.Name;
				if (!Param.AllowedValues.empty())
				{
					if (ComboFromList(Label.c_str(), Value, Param.AllowedValues))
					{
						Pending.Params[Param.Name] = Value;
					}
				}
				else
				{
					char Buffer[64] = {};
					std::snprintf(Buffer, sizeof(Buffer), "%s", Value.c_str());
					if (ImGui::InputText(Label.c_str(), Buffer, sizeof(Buffer)))
					{
						Pending.Params[Param.Name] = Buffer;
					}
				}
				if (ImGui::IsItemHovered() && !Param.Description.empty())
				{
					ImGui::SetTooltip("%s", Param.Description.c_str());
				}
			}
		}
		// T, the calling thread included: the bench's range (1..64, BenchConfig.cpp); the tooltip names this host's count.
		ImGui::SliderInt("Threads", &Pending.Threads, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Threads T of the scene, the calling thread included (1..64, as the bench). This host has %u logical CPUs.",
				std::thread::hardware_concurrency());
		}
		const bool bDirty = !Session.HasScene() || !(Pending == Session.GetSelection());
		ImGui::BeginDisabled(!bDirty);
		if (ImGui::Button("Apply (rebuild the scene)"))
		{
			RequestedOpen = Pending;
			bOpenRequested = true;
			bConfigListsStale = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Revert") && Session.HasScene())
		{
			Pending = Session.GetSelection();
		}

		// ---- restart required / failure ----
		if (LastOpenResult == EViewerOpenResult::RestartRequired)
		{
			ImGui::Separator();
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "Restart required");
			ImGui::TextWrapped("%s", LastOpenMessage.c_str());
			if (ImGui::Button("Restart the viewer with this selection"))
			{
				RequestRestart();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				LastOpenResult = EViewerOpenResult::Opened;
				LastOpenMessage.clear();
			}
		}
		else if (LastOpenResult == EViewerOpenResult::Failed)
		{
			ImGui::Separator();
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Open failed");
			ImGui::TextWrapped("%s", LastOpenMessage.c_str());
		}

		// ---- the backend in use ----
		if (Session.HasScene())
		{
			ImGui::Separator();
			const FBackendInfo& Info = Session.GetScene()->GetBackendInfo();
			ImGui::TextWrapped("%s (%s, %s)", Info.DisplayName.c_str(), Info.EngineVersion.c_str(), Info.BuildConfig.c_str());
			ImGui::Text("Module flags 0x%02x   Capabilities 0x%llx", Info.ModuleFlags, static_cast<unsigned long long>(Info.Capabilities));
			if (!Session.GetConfigFiles().VariantLabel.empty())
			{
				ImGui::TextWrapped("Variant: %s", Session.GetConfigFiles().VariantLabel.c_str());
			}
			ImGui::Text("Run mode: %s (viewer manifests are always viz)", ToString(Session.GetConfig().TestSettings.RunMode));
		}
		ImGui::End();
	}
}
