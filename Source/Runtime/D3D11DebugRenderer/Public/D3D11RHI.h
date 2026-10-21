// D3D11RHI.h - FD3D11RHI: the D3D11 device, the scene targets and the output (02_Architecture 11.1, A.11). [MVP M4]
// From the verified prototype (prototypes/render_d3d11/src/proto_render.cpp): hardware device (feature level 11_0), 4x MSAA
// RGBA8 colour + D32F depth for the scene, resolve into the output, readback to PNG (stb_image_write). The output is the
// window's swap chain back buffer (flip model) or, headless, an offscreen texture of the same format - the same code path
// renders both, so a headless screenshot shows exactly what the window shows. GPU time comes from timestamp queries.
// The bench exe never creates a device (CTest PhysicsTest.Probe.BenchNoRenderer); D3D types stay out of this header.
#pragma once

#include "CoreTypes.h"
#include "Math/Color.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace PhysicsTest
{
	struct FD3D11RHICreateParams
	{
		void* WindowHandle = nullptr;   // HWND; nullptr = headless (offscreen output)
		int32 Width = 1600;
		int32 Height = 900;
		int32 MsaaSamples = 4;
		bool bVSync = true;             // false: Present(0) with DXGI tearing when the system supports it (uncapped windowed rate)
		bool bDebugLayer = false;       // needs the Windows "Graphics Tools" feature; off by default
	};

	class FD3D11RHI
	{
	public:
		FD3D11RHI();
		~FD3D11RHI();
		FD3D11RHI(const FD3D11RHI&) = delete;
		FD3D11RHI& operator=(const FD3D11RHI&) = delete;

		bool Init(const FD3D11RHICreateParams& Params, std::string& OutError);
		void Shutdown();
		// Recreates the targets (and the swap chain buffers) for a new output size. No-op for the same size.
		bool Resize(int32 Width, int32 Height);

		// Binds the MSAA scene targets and clears colour (sRGB bytes written as is: the targets are UNORM) and depth (0 =
		// the far plane, reversed-Z).
		void BeginScene(const FColor& ClearColor);
		// MSAA colour -> output, then binds the output (no depth) for overlays (ImGui).
		void ResolveSceneToOutput();
		void Present();

		// Copies the output into CPU memory (RGBA8, top row first; alpha forced to 255).
		bool ReadOutputPixels(std::vector<uint8>& OutRgba, int32& OutWidth, int32& OutHeight);
		// Synchronous PNG of the output (02 11.2: asynchronous encoding is M12).
		bool CaptureOutputPng(const std::filesystem::path& Path, std::string& OutError);

		// GPU time between BeginGpuTimer and EndGpuTimer of the most recent frame whose queries are ready (ms; -1 = none yet).
		void BeginGpuTimer();
		void EndGpuTimer();
		[[nodiscard]] double GetLastGpuMilliseconds() const;
		// Blocks until the GPU has finished all submitted work (headless frame pacing, pessimistic fps).
		void WaitForGpu();

		[[nodiscard]] ID3D11Device* GetDevice() const;
		[[nodiscard]] ID3D11DeviceContext* GetContext() const;
		[[nodiscard]] int32 GetWidth() const;
		[[nodiscard]] int32 GetHeight() const;
		[[nodiscard]] int32 GetMsaaSamples() const;
		[[nodiscard]] bool IsHeadless() const;
		[[nodiscard]] const std::string& GetAdapterName() const;

	private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};
}
