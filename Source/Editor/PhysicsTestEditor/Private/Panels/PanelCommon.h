// PanelCommon.h - includes and small helpers shared by the editor panels (Private).
#pragma once

#include "CoreMinimal.h"
#include "D3D11DebugDrawRenderer.h"
#include "HAL/IConsoleManager.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsDebugSnapshot.h"
#include "PhysicsTestEditor.h"
#include "PhysicsViewportClient.h"
#include "PhysScene.h"
#include "ViewerSession.h"
#include "ViewportCameraController.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <imgui.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <string>

namespace PhysicsTest::EditorPanels
{
	inline const char* GetObjectStateName(const uint8 State)
	{
		switch (static_cast<EObjectStateType>(State))
		{
		case EObjectStateType::Dynamic: return "Dynamic";
		case EObjectStateType::Sleeping: return "Sleeping";
		case EObjectStateType::Kinematic: return "Kinematic";
		case EObjectStateType::Static: return "Static";
		default: return "Uninitialized";
		}
	}

	// "1.23 ms (viz mode, not a benchmark)"
	inline void TimingRow(const char* Label, const double Milliseconds)
	{
		if (Milliseconds < 0.0)
		{
			ImGui::Text("%-22s      n/a", Label);
			return;
		}
		ImGui::Text("%-22s %8.3f ms", Label, Milliseconds);
		ImGui::SameLine();
		ImGui::TextDisabled("(%s)", GVizTimingLabel);
	}

	inline void CounterRow(const char* Name, const long long Value)
	{
		if (Value < 0)
		{
			ImGui::Text("%-26s %10s", Name, "n/a");
		}
		else
		{
			ImGui::Text("%-26s %10lld", Name, Value);
		}
	}
}
