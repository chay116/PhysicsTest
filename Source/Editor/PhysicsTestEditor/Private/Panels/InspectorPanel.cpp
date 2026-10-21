// InspectorPanel.cpp - the Inspector panel: UE's World Outliner and Details panel in one (every body; the selected one).
#include "PanelCommon.h"

#include "Math/Rotator.h"

#include <cstring>

namespace PhysicsTest
{
	void FPhysicsTestEditor::DrawInspectorPanel(FEditorContext& Context)
	{
		ImGui::SetNextWindowPos(ImVec2(8.0f, 466.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(340.0f, 420.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Inspector", &bShowInspector))
		{
			ImGui::End();
			return;
		}
		const FViewerSession& Session = Context.Session;
		FPhysicsViewportClient& Viewport = Context.Viewport;
		if (!Session.HasScene())
		{
			ImGui::TextDisabled("no scene");
			ImGui::End();
			return;
		}
		const FPhysScene& Scene = *Session.GetScene();
		const FPhysicsDebugSnapshot& Snapshot = Viewport.GetSnapshot();
		const std::vector<int32>& ParticleOfBody = Viewport.GetParticleOfBody();

		// ---- outliner ----
		ImGui::InputTextWithHint("##filter", "filter by name", OutlinerFilter, sizeof(OutlinerFilter));
		const std::span<FBodyInstance* const> Bodies = Scene.GetBodies();
		std::vector<int32> Visible;
		Visible.reserve(Bodies.size());
		for (size_t Index = 0; Index < Bodies.size(); ++Index)
		{
			const FBodyInstance* Body = Bodies[Index];
			if (Body == nullptr || !Body->ActorHandle.IsValid())
			{
				continue;
			}
			if (OutlinerFilter[0] != '\0' && Body->BodyName.find(OutlinerFilter) == std::string::npos)
			{
				continue;
			}
			Visible.push_back(static_cast<int32>(Index));
		}
		ImGui::Text("%zu bodies (%zu shown)", Bodies.size(), Visible.size());
		if (ImGui::BeginChild("##outliner", ImVec2(0.0f, 170.0f), ImGuiChildFlags_Borders))
		{
			ImGuiListClipper Clipper;
			Clipper.Begin(static_cast<int>(Visible.size()));
			while (Clipper.Step())
			{
				for (int Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row)
				{
					const FBodyInstance* Body = Bodies[static_cast<size_t>(Visible[static_cast<size_t>(Row)])];
					const int32 BodyIndex = static_cast<int32>(Body->ActorHandle.Index);
					const char* State = EditorPanels::GetObjectStateName(static_cast<uint8>(Body->State.ObjectState));
					char Label[160];
					std::snprintf(Label, sizeof(Label), "#%d  %s  [%s]", BodyIndex, Body->BodyName.c_str(), State);
					if (ImGui::Selectable(Label, Viewport.SelectedBody == BodyIndex))
					{
						Viewport.SelectedBody = BodyIndex;
					}
				}
			}
		}
		ImGui::EndChild();

		// ---- details ----
		const FBodyInstance* Selected = Viewport.SelectedBody >= 0 ? Scene.GetBackendScene().GetBody(static_cast<uint32>(Viewport.SelectedBody)) : nullptr;
		if (Selected == nullptr)
		{
			ImGui::TextDisabled("select a body (click in the viewport or the list)");
			ImGui::End();
			return;
		}
		if (ImGui::Button("Frame (F)"))
		{
			bFocusRequested = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Deselect"))
		{
			Viewport.SelectedBody = INDEX_NONE;
			ImGui::End();
			return;
		}
		if (ImGui::BeginTable("##details", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			const auto Row = [](const char* Name, const char* Format, auto... Args)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(Name);
				ImGui::TableSetColumnIndex(1);
				ImGui::Text(Format, Args...);
			};
			const FTransform& BodyTM = Selected->State.BodyTM;
			const FRotator Rotation = BodyTM.Rotation.Rotator();
			Row("Name", "%s", Selected->BodyName.c_str());
			Row("Body index", "%u", Selected->ActorHandle.Index);
			Row("Mobility", "%s", Selected->IsStatic() ? "Static" : (Selected->IsKinematic() ? "Kinematic" : "Dynamic (simulated)"));
			Row("Object state", "%s", EditorPanels::GetObjectStateName(static_cast<uint8>(Selected->State.ObjectState)));
			Row("Location (cm)", "%.3f, %.3f, %.3f", BodyTM.Translation.X, BodyTM.Translation.Y, BodyTM.Translation.Z);
			Row("Rotation (P, Y, R)", "%.3f, %.3f, %.3f", Rotation.Pitch, Rotation.Yaw, Rotation.Roll);
			Row("Velocity (cm/s)", "%.3f, %.3f, %.3f", Selected->State.LinearVelocity.X, Selected->State.LinearVelocity.Y, Selected->State.LinearVelocity.Z);
			Row("Ang. vel. (rad/s)", "%.4f, %.4f, %.4f", Selected->State.AngularVelocityInRadians.X, Selected->State.AngularVelocityInRadians.Y,
				Selected->State.AngularVelocityInRadians.Z);
			Row("Mass (kg)", "%.4f", Selected->MassProperties.Mass);
			Row("COM (local, cm)", "%.3f, %.3f, %.3f", Selected->MassProperties.CenterOfMass.X, Selected->MassProperties.CenterOfMass.Y,
				Selected->MassProperties.CenterOfMass.Z);
			Row("Inertia (kg cm2)", "%.1f, %.1f, %.1f", Selected->MassProperties.InertiaTensor.X, Selected->MassProperties.InertiaTensor.Y,
				Selected->MassProperties.InertiaTensor.Z);
			if (const FPhysicalMaterial* Material = Selected->GetSimplePhysicalMaterial())
			{
				Row("Material", "%s  mu %.3f / %.3f  e %.3f", Material->Name.c_str(), Material->Friction, Material->StaticFriction, Material->Restitution);
			}
			Row("Damping (lin / ang)", "%.4f / %.4f", Selected->LinearDamping, Selected->AngularDamping);
			const size_t BodySlot = static_cast<size_t>(Viewport.SelectedBody);
			if (BodySlot < ParticleOfBody.size() && ParticleOfBody[BodySlot] != INDEX_NONE)
			{
				const size_t Slot = static_cast<size_t>(ParticleOfBody[BodySlot]);
				const PtParticleSnapshot& Particle = Snapshot.Particles[Slot];
				Row("Snapshot", "%s", Snapshot.bFromHostState ? "host state (backend has none)" : "backend");
				Row("Bounds min", "%.2f, %.2f, %.2f", Particle.BoundsMin.X, Particle.BoundsMin.Y, Particle.BoundsMin.Z);
				Row("Bounds max", "%.2f, %.2f, %.2f", Particle.BoundsMax.X, Particle.BoundsMax.Y, Particle.BoundsMax.Z);
				if (Viewport.HasIslands() && Slot < Snapshot.HarnessIslandIndex.size())
				{
					Row("Island (harness)", "%d", Snapshot.HarnessIslandIndex[Slot]);
				}
				int32 NumContacts = 0;
				double MinPhi = 0.0;
				for (const PtContactPoint& Contact : Snapshot.Contacts)
				{
					if (Contact.Body0 == Particle.BodyIndex || Contact.Body1 == Particle.BodyIndex)
					{
						MinPhi = NumContacts == 0 ? Contact.Phi : FMath::Min(MinPhi, Contact.Phi);
						++NumContacts;
					}
				}
				if (Viewport.HasIslands())
				{
					Row("Contact points", "%d (min Phi %.3f cm)", NumContacts, MinPhi);
				}
			}
			ImGui::EndTable();
		}
		ImGui::End();
	}
}
