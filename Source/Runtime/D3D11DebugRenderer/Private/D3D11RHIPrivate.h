// D3D11RHIPrivate.h - the D3D11 objects behind FD3D11RHI, shared with the renderer TU of this module only.
#pragma once

#include "D3D11RHI.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <array>
#include <string>

namespace PhysicsTest
{
	using Microsoft::WRL::ComPtr;

	struct FD3D11RHI::FImpl
	{
		struct FGpuTimerSet
		{
			ComPtr<ID3D11Query> Disjoint;
			ComPtr<ID3D11Query> Begin;
			ComPtr<ID3D11Query> End;
			bool bPending = false;
		};

		FD3D11RHICreateParams Params;
		ComPtr<ID3D11Device> Device;
		ComPtr<ID3D11DeviceContext> Context;
		ComPtr<IDXGISwapChain1> SwapChain;
		ComPtr<ID3D11Texture2D> Output;            // back buffer or offscreen texture (RGBA8, 1 sample)
		ComPtr<ID3D11RenderTargetView> OutputRtv;
		ComPtr<ID3D11Texture2D> SceneColor;        // MSAA RGBA8
		ComPtr<ID3D11RenderTargetView> SceneRtv;
		ComPtr<ID3D11Texture2D> SceneDepth;        // MSAA D32F
		ComPtr<ID3D11DepthStencilView> SceneDsv;
		ComPtr<ID3D11Texture2D> Staging;
		ComPtr<ID3D11Query> EventQuery;
		std::array<FGpuTimerSet, 4> Timers;
		uint32 TimerWrite = 0;
		double LastGpuMilliseconds = -1.0;
		std::string AdapterName;
		int32 Width = 0;
		int32 Height = 0;
		int32 Samples = 4;
		UINT SwapChainFlags = 0;                   // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING for -NoVSync when the system supports it

		bool CreateTargets(std::string& OutError);
		void ReleaseTargets();
		void CollectTimers(bool bWait);
	};
}
