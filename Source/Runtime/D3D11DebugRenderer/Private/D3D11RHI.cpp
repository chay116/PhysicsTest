// D3D11RHI.cpp - FD3D11RHI (see D3D11RHI.h). Ported from prototypes/render_d3d11/src/proto_render.cpp (device, MSAA
// targets, resolve, readback, PNG, timestamp queries) and extended with the swap chain and resizing.
#include "D3D11RHIPrivate.h"

#include "Containers/StringConv.h"
#include "Logging/LogMacros.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <cstring>

namespace PhysicsTest
{
	namespace
	{
		std::string HrText(const char* What, const HRESULT Result)
		{
			char Text[160];
			std::snprintf(Text, sizeof(Text), "%s failed (HRESULT 0x%08lx)", What, static_cast<unsigned long>(Result));
			return Text;
		}

		constexpr DXGI_FORMAT GColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	FD3D11RHI::FD3D11RHI()
		: Impl(std::make_unique<FImpl>())
	{
	}

	FD3D11RHI::~FD3D11RHI()
	{
		Shutdown();
	}

	bool FD3D11RHI::Init(const FD3D11RHICreateParams& Params, std::string& OutError)
	{
		Impl->Params = Params;
		Impl->Width = Params.Width > 0 ? Params.Width : 1;
		Impl->Height = Params.Height > 0 ? Params.Height : 1;
		Impl->Samples = Params.MsaaSamples > 0 ? Params.MsaaSamples : 1;
		UINT Flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		if (Params.bDebugLayer)
		{
			Flags |= D3D11_CREATE_DEVICE_DEBUG;
		}
		const D3D_FEATURE_LEVEL Wanted[] = { D3D_FEATURE_LEVEL_11_0 };
		D3D_FEATURE_LEVEL Level = {};
		HRESULT Result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, Flags, Wanted, 1, D3D11_SDK_VERSION, Impl->Device.GetAddressOf(), &Level,
			Impl->Context.GetAddressOf());
		if (FAILED(Result))
		{
			OutError = HrText("D3D11CreateDevice (hardware, feature level 11_0)", Result);
			return false;
		}
		{
			ComPtr<IDXGIDevice> DxgiDevice;
			ComPtr<IDXGIAdapter> Adapter;
			DXGI_ADAPTER_DESC Desc = {};
			if (SUCCEEDED(Impl->Device.As(&DxgiDevice)) && SUCCEEDED(DxgiDevice->GetAdapter(Adapter.GetAddressOf())) && SUCCEEDED(Adapter->GetDesc(&Desc)))
			{
				Impl->AdapterName = StringConv::WideToUtf8(Desc.Description);
			}
			if (Params.WindowHandle != nullptr)
			{
				ComPtr<IDXGIFactory2> Factory;
				Result = Adapter ? Adapter->GetParent(IID_PPV_ARGS(Factory.GetAddressOf())) : E_FAIL;
				if (FAILED(Result))
				{
					OutError = HrText("IDXGIAdapter::GetParent(IDXGIFactory2)", Result);
					return false;
				}
				DXGI_SWAP_CHAIN_DESC1 SwapDesc = {};
				SwapDesc.Width = static_cast<UINT>(Impl->Width);
				SwapDesc.Height = static_cast<UINT>(Impl->Height);
				SwapDesc.Format = GColorFormat;
				SwapDesc.SampleDesc.Count = 1;
				SwapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				SwapDesc.BufferCount = 2;
				SwapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				// Without vsync a flip-model swap chain still waits for the refresh unless tearing is allowed: request it when the
				// system supports it (variable refresh / tearing, IDXGIFactory5::CheckFeatureSupport), so -NoVSync uncaps the
				// windowed frame rate.
				if (!Params.bVSync)
				{
					ComPtr<IDXGIFactory5> Factory5;
					BOOL bAllowTearing = FALSE;
					if (SUCCEEDED(Factory.As(&Factory5))
						&& SUCCEEDED(Factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &bAllowTearing, sizeof(bAllowTearing))) && bAllowTearing)
					{
						Impl->SwapChainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
					}
					else
					{
						UE_LOG(LogPhysicsTest, Warning, "D3D11: -NoVSync without tearing support; the windowed frame rate stays at the refresh rate");
					}
				}
				SwapDesc.Flags = Impl->SwapChainFlags;
				Result = Factory->CreateSwapChainForHwnd(Impl->Device.Get(), static_cast<HWND>(Params.WindowHandle), &SwapDesc, nullptr, nullptr,
					Impl->SwapChain.GetAddressOf());
				if (FAILED(Result))
				{
					OutError = HrText("CreateSwapChainForHwnd", Result);
					return false;
				}
				Factory->MakeWindowAssociation(static_cast<HWND>(Params.WindowHandle), DXGI_MWA_NO_ALT_ENTER);
			}
		}
		UINT Quality = 0;
		if (FAILED(Impl->Device->CheckMultisampleQualityLevels(GColorFormat, static_cast<UINT>(Impl->Samples), &Quality)) || Quality == 0)
		{
			UE_LOG(LogPhysicsTest, Warning, "D3D11: %dx MSAA is not supported for RGBA8; using 1 sample", Impl->Samples);
			Impl->Samples = 1;
		}
		for (FImpl::FGpuTimerSet& Set : Impl->Timers)
		{
			D3D11_QUERY_DESC QueryDesc = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
			Impl->Device->CreateQuery(&QueryDesc, Set.Disjoint.GetAddressOf());
			QueryDesc.Query = D3D11_QUERY_TIMESTAMP;
			Impl->Device->CreateQuery(&QueryDesc, Set.Begin.GetAddressOf());
			Impl->Device->CreateQuery(&QueryDesc, Set.End.GetAddressOf());
		}
		D3D11_QUERY_DESC EventDesc = { D3D11_QUERY_EVENT, 0 };
		Impl->Device->CreateQuery(&EventDesc, Impl->EventQuery.GetAddressOf());
		return Impl->CreateTargets(OutError);
	}

	void FD3D11RHI::Shutdown()
	{
		if (!Impl || !Impl->Device)
		{
			return;
		}
		if (Impl->Context)
		{
			Impl->Context->ClearState();
			Impl->Context->Flush();
		}
		Impl->ReleaseTargets();
		for (FImpl::FGpuTimerSet& Set : Impl->Timers)
		{
			Set = FImpl::FGpuTimerSet();
		}
		Impl->EventQuery.Reset();
		Impl->SwapChain.Reset();
		Impl->Context.Reset();
		Impl->Device.Reset();
	}

	bool FD3D11RHI::FImpl::CreateTargets(std::string& OutError)
	{
		HRESULT Result = S_OK;
		if (SwapChain)
		{
			Result = SwapChain->GetBuffer(0, IID_PPV_ARGS(Output.GetAddressOf()));
			if (FAILED(Result))
			{
				OutError = HrText("IDXGISwapChain1::GetBuffer", Result);
				return false;
			}
		}
		else
		{
			D3D11_TEXTURE2D_DESC Desc = {};
			Desc.Width = static_cast<UINT>(Width);
			Desc.Height = static_cast<UINT>(Height);
			Desc.MipLevels = 1;
			Desc.ArraySize = 1;
			Desc.Format = GColorFormat;
			Desc.SampleDesc.Count = 1;
			Desc.Usage = D3D11_USAGE_DEFAULT;
			Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			Result = Device->CreateTexture2D(&Desc, nullptr, Output.GetAddressOf());
			if (FAILED(Result))
			{
				OutError = HrText("CreateTexture2D (offscreen output)", Result);
				return false;
			}
		}
		Result = Device->CreateRenderTargetView(Output.Get(), nullptr, OutputRtv.GetAddressOf());
		if (FAILED(Result))
		{
			OutError = HrText("CreateRenderTargetView (output)", Result);
			return false;
		}
		D3D11_TEXTURE2D_DESC Desc = {};
		Desc.Width = static_cast<UINT>(Width);
		Desc.Height = static_cast<UINT>(Height);
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;
		Desc.Format = GColorFormat;
		Desc.SampleDesc.Count = static_cast<UINT>(Samples);
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		Result = Device->CreateTexture2D(&Desc, nullptr, SceneColor.GetAddressOf());
		if (SUCCEEDED(Result))
		{
			Result = Device->CreateRenderTargetView(SceneColor.Get(), nullptr, SceneRtv.GetAddressOf());
		}
		if (FAILED(Result))
		{
			OutError = HrText("scene colour target", Result);
			return false;
		}
		Desc.Format = DXGI_FORMAT_D32_FLOAT;
		Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		Result = Device->CreateTexture2D(&Desc, nullptr, SceneDepth.GetAddressOf());
		if (SUCCEEDED(Result))
		{
			Result = Device->CreateDepthStencilView(SceneDepth.Get(), nullptr, SceneDsv.GetAddressOf());
		}
		if (FAILED(Result))
		{
			OutError = HrText("scene depth target", Result);
			return false;
		}
		Desc.Format = GColorFormat;
		Desc.SampleDesc.Count = 1;
		Desc.Usage = D3D11_USAGE_STAGING;
		Desc.BindFlags = 0;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Result = Device->CreateTexture2D(&Desc, nullptr, Staging.GetAddressOf());
		if (FAILED(Result))
		{
			OutError = HrText("staging texture", Result);
			return false;
		}
		return true;
	}

	void FD3D11RHI::FImpl::ReleaseTargets()
	{
		if (Context)
		{
			Context->OMSetRenderTargets(0, nullptr, nullptr);
		}
		Staging.Reset();
		SceneDsv.Reset();
		SceneDepth.Reset();
		SceneRtv.Reset();
		SceneColor.Reset();
		OutputRtv.Reset();
		Output.Reset();
	}

	bool FD3D11RHI::Resize(const int32 Width, const int32 Height)
	{
		if (Width <= 0 || Height <= 0 || (Width == Impl->Width && Height == Impl->Height))
		{
			return true;
		}
		Impl->ReleaseTargets();
		Impl->Width = Width;
		Impl->Height = Height;
		if (Impl->SwapChain)
		{
			const HRESULT Result = Impl->SwapChain->ResizeBuffers(0, static_cast<UINT>(Width), static_cast<UINT>(Height), DXGI_FORMAT_UNKNOWN, Impl->SwapChainFlags);
			if (FAILED(Result))
			{
				UE_LOG(LogPhysicsTest, Error, "%s", HrText("IDXGISwapChain1::ResizeBuffers", Result).c_str());
				return false;
			}
		}
		std::string Error;
		if (!Impl->CreateTargets(Error))
		{
			UE_LOG(LogPhysicsTest, Error, "D3D11 resize: %s", Error.c_str());
			return false;
		}
		return true;
	}

	void FD3D11RHI::BeginScene(const FColor& ClearColor)
	{
		ID3D11DeviceContext* Context = Impl->Context.Get();
		const float Clear[4] = { ClearColor.R / 255.0f, ClearColor.G / 255.0f, ClearColor.B / 255.0f, 1.0f };
		Context->ClearRenderTargetView(Impl->SceneRtv.Get(), Clear);
		Context->ClearDepthStencilView(Impl->SceneDsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
		ID3D11RenderTargetView* Rtv = Impl->SceneRtv.Get();
		Context->OMSetRenderTargets(1, &Rtv, Impl->SceneDsv.Get());
		const D3D11_VIEWPORT Viewport = { 0.0f, 0.0f, static_cast<float>(Impl->Width), static_cast<float>(Impl->Height), 0.0f, 1.0f };
		Context->RSSetViewports(1, &Viewport);
	}

	void FD3D11RHI::ResolveSceneToOutput()
	{
		ID3D11DeviceContext* Context = Impl->Context.Get();
		if (Impl->Samples > 1)
		{
			Context->ResolveSubresource(Impl->Output.Get(), 0, Impl->SceneColor.Get(), 0, GColorFormat);
		}
		else
		{
			Context->CopyResource(Impl->Output.Get(), Impl->SceneColor.Get());
		}
		ID3D11RenderTargetView* Rtv = Impl->OutputRtv.Get();
		Context->OMSetRenderTargets(1, &Rtv, nullptr);
	}

	void FD3D11RHI::Present()
	{
		if (Impl->SwapChain)
		{
			const bool bTearing = !Impl->Params.bVSync && (Impl->SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0;
			Impl->SwapChain->Present(Impl->Params.bVSync ? 1u : 0u, bTearing ? DXGI_PRESENT_ALLOW_TEARING : 0u);
		}
		else
		{
			Impl->Context->Flush();
		}
	}

	bool FD3D11RHI::ReadOutputPixels(std::vector<uint8>& OutRgba, int32& OutWidth, int32& OutHeight)
	{
		ID3D11DeviceContext* Context = Impl->Context.Get();
		Context->CopyResource(Impl->Staging.Get(), Impl->Output.Get());
		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (FAILED(Context->Map(Impl->Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped)))
		{
			return false;
		}
		OutWidth = Impl->Width;
		OutHeight = Impl->Height;
		OutRgba.resize(static_cast<size_t>(OutWidth) * static_cast<size_t>(OutHeight) * 4u);
		for (int32 Row = 0; Row < OutHeight; ++Row)
		{
			std::memcpy(&OutRgba[static_cast<size_t>(Row) * static_cast<size_t>(OutWidth) * 4u], static_cast<const uint8*>(Mapped.pData) + static_cast<size_t>(Row) * Mapped.RowPitch,
				static_cast<size_t>(OutWidth) * 4u);
		}
		Context->Unmap(Impl->Staging.Get(), 0);
		for (size_t Pixel = 3; Pixel < OutRgba.size(); Pixel += 4)
		{
			OutRgba[Pixel] = 255;
		}
		return true;
	}

	bool FD3D11RHI::CaptureOutputPng(const std::filesystem::path& Path, std::string& OutError)
	{
		std::vector<uint8> Pixels;
		int32 Width = 0;
		int32 Height = 0;
		if (!ReadOutputPixels(Pixels, Width, Height))
		{
			OutError = "ID3D11DeviceContext::Map of the staging texture failed";
			return false;
		}
		std::error_code Ignored;
		if (Path.has_parent_path())
		{
			std::filesystem::create_directories(Path.parent_path(), Ignored);
		}
		// stb_image_write takes a narrow path; write to memory, then the file with a wide path (non-ASCII directories).
		std::vector<uint8> Png;
		const auto Writer = [](void* Context, void* Data, const int Size)
		{
			std::vector<uint8>& Out = *static_cast<std::vector<uint8>*>(Context);
			Out.insert(Out.end(), static_cast<const uint8*>(Data), static_cast<const uint8*>(Data) + Size);
		};
		if (stbi_write_png_to_func(Writer, &Png, Width, Height, 4, Pixels.data(), Width * 4) == 0)
		{
			OutError = "stbi_write_png_to_func failed";
			return false;
		}
		FILE* File = nullptr;
		if (_wfopen_s(&File, Path.c_str(), L"wb") != 0 || File == nullptr)
		{
			OutError = "cannot open '" + StringConv::PathToUtf8(Path) + "' for writing";
			return false;
		}
		const size_t Written = std::fwrite(Png.data(), 1, Png.size(), File);
		std::fclose(File);
		if (Written != Png.size())
		{
			OutError = "short write to '" + StringConv::PathToUtf8(Path) + "'";
			return false;
		}
		return true;
	}

	void FD3D11RHI::BeginGpuTimer()
	{
		FImpl::FGpuTimerSet& Set = Impl->Timers[Impl->TimerWrite % Impl->Timers.size()];
		if (Set.bPending)
		{
			Impl->CollectTimers(true);   // the ring is full: wait for the oldest set
		}
		Impl->Context->Begin(Set.Disjoint.Get());
		Impl->Context->End(Set.Begin.Get());
	}

	void FD3D11RHI::EndGpuTimer()
	{
		FImpl::FGpuTimerSet& Set = Impl->Timers[Impl->TimerWrite % Impl->Timers.size()];
		Impl->Context->End(Set.End.Get());
		Impl->Context->End(Set.Disjoint.Get());
		Set.bPending = true;
		++Impl->TimerWrite;
		Impl->CollectTimers(false);
	}

	void FD3D11RHI::FImpl::CollectTimers(const bool bWait)
	{
		// Oldest first: TimerWrite is the next slot to write, i.e. the oldest one of the ring, so the scan starts there and wraps.
		for (size_t Offset = 0; Offset < Timers.size(); ++Offset)
		{
			FGpuTimerSet& Set = Timers[(TimerWrite + Offset) % Timers.size()];
			if (!Set.bPending)
			{
				continue;
			}
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT Disjoint = {};
			UINT64 Begin = 0;
			UINT64 End = 0;
			const UINT Flags = bWait ? 0u : static_cast<UINT>(D3D11_ASYNC_GETDATA_DONOTFLUSH);
			HRESULT Result = S_FALSE;
			do
			{
				Result = Context->GetData(Set.Disjoint.Get(), &Disjoint, sizeof(Disjoint), Flags);
			} while (bWait && Result == S_FALSE);
			if (Result != S_OK)
			{
				return;
			}
			if (Context->GetData(Set.Begin.Get(), &Begin, sizeof(Begin), Flags) != S_OK || Context->GetData(Set.End.Get(), &End, sizeof(End), Flags) != S_OK)
			{
				return;
			}
			Set.bPending = false;
			if (!Disjoint.Disjoint && Disjoint.Frequency > 0 && End >= Begin)
			{
				LastGpuMilliseconds = static_cast<double>(End - Begin) * 1000.0 / static_cast<double>(Disjoint.Frequency);
			}
			if (bWait)
			{
				return;
			}
		}
	}

	double FD3D11RHI::GetLastGpuMilliseconds() const
	{
		return Impl->LastGpuMilliseconds;
	}

	void FD3D11RHI::WaitForGpu()
	{
		Impl->Context->End(Impl->EventQuery.Get());
		BOOL bDone = FALSE;
		HRESULT Result = S_FALSE;
		do
		{
			Result = Impl->Context->GetData(Impl->EventQuery.Get(), &bDone, sizeof(bDone), 0);
		} while (Result == S_FALSE);   // S_OK when done; an error (device removed) ends the wait too
		// Every timer set is complete now.
		for (size_t Index = 0; Index < Impl->Timers.size(); ++Index)
		{
			Impl->CollectTimers(false);
		}
	}

	ID3D11Device* FD3D11RHI::GetDevice() const { return Impl->Device.Get(); }
	ID3D11DeviceContext* FD3D11RHI::GetContext() const { return Impl->Context.Get(); }
	int32 FD3D11RHI::GetWidth() const { return Impl->Width; }
	int32 FD3D11RHI::GetHeight() const { return Impl->Height; }
	int32 FD3D11RHI::GetMsaaSamples() const { return Impl->Samples; }
	bool FD3D11RHI::IsHeadless() const { return !Impl->SwapChain; }
	const std::string& FD3D11RHI::GetAdapterName() const { return Impl->AdapterName; }
}
