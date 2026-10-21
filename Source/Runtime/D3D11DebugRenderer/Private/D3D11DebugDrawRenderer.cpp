// D3D11DebugDrawRenderer.cpp - FD3D11DebugDrawRenderer (see the header). D3D11 code ported from the verified prototype
// (prototypes/render_d3d11/src/proto_render.cpp); the primitive topology follows UE's line batcher (WireMeshes.h).
#include "D3D11DebugDrawRenderer.h"

#include "D3D11RHIPrivate.h"
#include "WireMeshes.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <d3dcompiler.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <cmath>
#include <cstring>

namespace PhysicsTest
{
	namespace
	{
		const char* const GShaderSource =
#include "Shaders.hlsl.inl"
			;

		struct alignas(16) FViewConstants
		{
			float ViewProj[4][4];
			float Viewport[2];
			float NearW;
			float PixelsPerUnitAtW1;
		};
		struct alignas(16) FDrawConstants
		{
			uint32 SegBase;
			uint32 ItemBase;
			uint32 Pad[2];
		};
		static_assert(sizeof(FD3D11DebugDrawRenderer::FGpuInstance) == 80, "instance layout");
		static_assert(sizeof(FD3D11DebugDrawRenderer::FGpuLine) == 32, "line layout");
		static_assert(sizeof(FD3D11DebugDrawRenderer::FGpuPoint) == 32, "point layout");

		bool CompileShader(const char* Entry, const char* Target, ComPtr<ID3DBlob>& OutCode, std::string& OutError)
		{
			ComPtr<ID3DBlob> Errors;
			const HRESULT Result = D3DCompile(GShaderSource, std::strlen(GShaderSource), "Shaders.hlsl", nullptr, nullptr, Entry, Target,
				D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, OutCode.GetAddressOf(), Errors.GetAddressOf());
			if (FAILED(Result))
			{
				OutError = std::string("HLSL ") + Entry + ": " + (Errors ? static_cast<const char*>(Errors->GetBufferPointer()) : "D3DCompile failed");
				return false;
			}
			return true;
		}

		// A dynamic structured buffer that grows by doubling (CPU write each frame).
		struct FDynamicBuffer
		{
			ComPtr<ID3D11Buffer> Buffer;
			ComPtr<ID3D11ShaderResourceView> Srv;
			uint32 Capacity = 0;
			uint32 Stride = 0;

			bool Upload(ID3D11Device* Device, ID3D11DeviceContext* Context, const void* Data, const uint32 Count, const uint32 InStride, int64& InOutBytes)
			{
				if (Count == 0)
				{
					return true;
				}
				if (Count > Capacity || InStride != Stride)
				{
					uint32 NewCapacity = Capacity > 0 ? Capacity : 1024u;
					while (NewCapacity < Count)
					{
						NewCapacity *= 2u;
					}
					D3D11_BUFFER_DESC Desc = {};
					Desc.ByteWidth = NewCapacity * InStride;
					Desc.Usage = D3D11_USAGE_DYNAMIC;
					Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
					Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
					Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
					Desc.StructureByteStride = InStride;
					Buffer.Reset();
					Srv.Reset();
					if (FAILED(Device->CreateBuffer(&Desc, nullptr, Buffer.GetAddressOf())) || FAILED(Device->CreateShaderResourceView(Buffer.Get(), nullptr, Srv.GetAddressOf())))
					{
						Capacity = 0;
						return false;
					}
					Capacity = NewCapacity;
					Stride = InStride;
				}
				D3D11_MAPPED_SUBRESOURCE Mapped = {};
				if (FAILED(Context->Map(Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
				{
					return false;
				}
				std::memcpy(Mapped.pData, Data, static_cast<size_t>(Count) * InStride);
				Context->Unmap(Buffer.Get(), 0);
				InOutBytes += static_cast<int64>(Count) * InStride;
				return true;
			}
		};

		void ToFloat3(const FVector& V, float (&Out)[3])
		{
			Out[0] = static_cast<float>(V.X);
			Out[1] = static_cast<float>(V.Y);
			Out[2] = static_cast<float>(V.Z);
		}
	}

	struct FD3D11DebugDrawRenderer::FImpl
	{
		ComPtr<ID3D11VertexShader> VSInstanced;
		ComPtr<ID3D11VertexShader> VSLines;
		ComPtr<ID3D11VertexShader> VSPoints;
		ComPtr<ID3D11PixelShader> PSMain;
		ComPtr<ID3D11RasterizerState> Rasterizer;
		ComPtr<ID3D11DepthStencilState> DepthTest;      // GreaterEqual (reversed-Z NearOrEqual), write
		ComPtr<ID3D11DepthStencilState> DepthAlways;    // Always, write (SeeThrough first pass)
		ComPtr<ID3D11BlendState> Opaque;
		ComPtr<ID3D11Buffer> ViewConstants;
		ComPtr<ID3D11Buffer> DrawConstants;
		ComPtr<ID3D11Buffer> Segments;
		ComPtr<ID3D11ShaderResourceView> SegmentsSrv;
		uint32 MeshFirst[Mesh_Count] = {};      // first float4 of each unit mesh
		uint32 MeshSegments[Mesh_Count] = {};
		FDynamicBuffer InstanceBuffer;
		FDynamicBuffer LineBuffer;
		FDynamicBuffer PointBuffer;
		std::vector<FGpuInstance> AllInstances;  // the per-mesh batches back to back (one upload)
		std::vector<FGpuLine> AllLines;          // grid lines, then the debug lines
	};

	FD3D11DebugDrawRenderer::FD3D11DebugDrawRenderer(FD3D11RHI& InRHI)
		: Impl(std::make_unique<FImpl>())
		, RHI(InRHI)
	{
	}

	FD3D11DebugDrawRenderer::~FD3D11DebugDrawRenderer() = default;

	bool FD3D11DebugDrawRenderer::Init(std::string& OutError)
	{
		ID3D11Device* Device = RHI.GetDevice();
		if (Device == nullptr)
		{
			OutError = "FD3D11DebugDrawRenderer::Init before FD3D11RHI::Init";
			return false;
		}
		ComPtr<ID3DBlob> Code;
		if (!CompileShader("VSInstanced", "vs_5_0", Code, OutError)
			|| FAILED(Device->CreateVertexShader(Code->GetBufferPointer(), Code->GetBufferSize(), nullptr, Impl->VSInstanced.GetAddressOf())))
		{
			OutError = OutError.empty() ? "CreateVertexShader(VSInstanced)" : OutError;
			return false;
		}
		Code.Reset();
		if (!CompileShader("VSLines", "vs_5_0", Code, OutError)
			|| FAILED(Device->CreateVertexShader(Code->GetBufferPointer(), Code->GetBufferSize(), nullptr, Impl->VSLines.GetAddressOf())))
		{
			OutError = OutError.empty() ? "CreateVertexShader(VSLines)" : OutError;
			return false;
		}
		Code.Reset();
		if (!CompileShader("VSPoints", "vs_5_0", Code, OutError)
			|| FAILED(Device->CreateVertexShader(Code->GetBufferPointer(), Code->GetBufferSize(), nullptr, Impl->VSPoints.GetAddressOf())))
		{
			OutError = OutError.empty() ? "CreateVertexShader(VSPoints)" : OutError;
			return false;
		}
		Code.Reset();
		if (!CompileShader("PSMain", "ps_5_0", Code, OutError)
			|| FAILED(Device->CreatePixelShader(Code->GetBufferPointer(), Code->GetBufferSize(), nullptr, Impl->PSMain.GetAddressOf())))
		{
			OutError = OutError.empty() ? "CreatePixelShader(PSMain)" : OutError;
			return false;
		}

		D3D11_RASTERIZER_DESC RasterDesc = {};
		RasterDesc.FillMode = D3D11_FILL_SOLID;
		RasterDesc.CullMode = D3D11_CULL_NONE;
		RasterDesc.DepthClipEnable = TRUE;
		RasterDesc.MultisampleEnable = TRUE;
		Device->CreateRasterizerState(&RasterDesc, Impl->Rasterizer.GetAddressOf());
		D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
		DepthDesc.DepthEnable = TRUE;
		DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		DepthDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
		Device->CreateDepthStencilState(&DepthDesc, Impl->DepthTest.GetAddressOf());
		DepthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		Device->CreateDepthStencilState(&DepthDesc, Impl->DepthAlways.GetAddressOf());
		D3D11_BLEND_DESC BlendDesc = {};
		BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		Device->CreateBlendState(&BlendDesc, Impl->Opaque.GetAddressOf());

		D3D11_BUFFER_DESC ConstantDesc = {};
		ConstantDesc.Usage = D3D11_USAGE_DYNAMIC;
		ConstantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		ConstantDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ConstantDesc.ByteWidth = sizeof(FViewConstants);
		Device->CreateBuffer(&ConstantDesc, nullptr, Impl->ViewConstants.GetAddressOf());
		ConstantDesc.ByteWidth = sizeof(FDrawConstants);
		Device->CreateBuffer(&ConstantDesc, nullptr, Impl->DrawConstants.GetAddressOf());

		// The unit meshes, back to back in one immutable buffer.
		std::vector<DebugDraw::FWireVertex> All;
		const auto AddMesh = [&](const EMesh Mesh, auto&& Build)
		{
			Impl->MeshFirst[Mesh] = static_cast<uint32>(All.size());
			Build(All);
			Impl->MeshSegments[Mesh] = (static_cast<uint32>(All.size()) - Impl->MeshFirst[Mesh]) / 2u;
		};
		AddMesh(Mesh_Box, [](std::vector<DebugDraw::FWireVertex>& Out) { DebugDraw::AppendUnitBox(Out); });
		AddMesh(Mesh_Sphere, [](std::vector<DebugDraw::FWireVertex>& Out) { DebugDraw::AppendUnitSphere(DebugDraw::GSphereSegments, Out); });
		AddMesh(Mesh_Capsule, [](std::vector<DebugDraw::FWireVertex>& Out) { DebugDraw::AppendUnitCapsule(DebugDraw::GCapsuleSides, Out); });
		AddMesh(Mesh_Circle, [](std::vector<DebugDraw::FWireVertex>& Out) { DebugDraw::AppendUnitCircle(DebugDraw::GCircleSegments, Out); });
		D3D11_BUFFER_DESC SegDesc = {};
		SegDesc.ByteWidth = static_cast<UINT>(All.size() * sizeof(DebugDraw::FWireVertex));
		SegDesc.Usage = D3D11_USAGE_IMMUTABLE;
		SegDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		SegDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		SegDesc.StructureByteStride = sizeof(DebugDraw::FWireVertex);
		const D3D11_SUBRESOURCE_DATA SegData = { All.data(), 0, 0 };
		if (FAILED(Device->CreateBuffer(&SegDesc, &SegData, Impl->Segments.GetAddressOf()))
			|| FAILED(Device->CreateShaderResourceView(Impl->Segments.Get(), nullptr, Impl->SegmentsSrv.GetAddressOf())))
		{
			OutError = "unit wire mesh buffer";
			return false;
		}
		return true;
	}

	void FD3D11DebugDrawRenderer::BeginView(const FSceneView& InView, const FSphere& InDrawRegion)
	{
		View = InView;
		DrawRegion = InDrawRegion;
		for (std::vector<FGpuInstance>& Batch : Instances)
		{
			Batch.clear();
		}
		Lines.clear();
		GridLines.clear();
		Points.clear();
		Strings.clear();
		Stats = FD3D11DebugDrawStats();
	}

	void FD3D11DebugDrawRenderer::AddInstance(const EMesh Mesh, const FVector& Center, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ,
		const FVector& Scale, const double CapCentreOffset, const FColor& Color, const float Thickness)
	{
		FGpuInstance& Instance = Instances[Mesh].emplace_back();
		const FVector Relative = Center - View.ViewOrigin;
		// Rows of [AxisX AxisY AxisZ] (the axes are the columns) with the translation in w; the scale is applied in the shader.
		Instance.R0[0] = static_cast<float>(AxisX.X);
		Instance.R0[1] = static_cast<float>(AxisY.X);
		Instance.R0[2] = static_cast<float>(AxisZ.X);
		Instance.R0[3] = static_cast<float>(Relative.X);
		Instance.R1[0] = static_cast<float>(AxisX.Y);
		Instance.R1[1] = static_cast<float>(AxisY.Y);
		Instance.R1[2] = static_cast<float>(AxisZ.Y);
		Instance.R1[3] = static_cast<float>(Relative.Y);
		Instance.R2[0] = static_cast<float>(AxisX.Z);
		Instance.R2[1] = static_cast<float>(AxisY.Z);
		Instance.R2[2] = static_cast<float>(AxisZ.Z);
		Instance.R2[3] = static_cast<float>(Relative.Z);
		Instance.Params[0] = static_cast<float>(Scale.X);
		Instance.Params[1] = static_cast<float>(Scale.Y);
		Instance.Params[2] = static_cast<float>(Scale.Z);
		Instance.Params[3] = static_cast<float>(CapCentreOffset);
		Instance.Color = Color.ToPackedABGR();
		Instance.Thickness = Thickness;
		Instance.Pad[0] = 0;
		Instance.Pad[1] = 0;
	}

	void FD3D11DebugDrawRenderer::AddLine(std::vector<FGpuLine>& Target, const FVector& A, const FVector& B, const FColor& Color, const float Thickness)
	{
		FGpuLine& Line = Target.emplace_back();
		ToFloat3(A - View.ViewOrigin, Line.A);
		ToFloat3(B - View.ViewOrigin, Line.B);
		Line.Color = Color.ToPackedABGR();
		Line.Thickness = Thickness;
	}

	void FD3D11DebugDrawRenderer::RenderPoint(const FVector& Position, const FColor& Color, const float PointSize, const float /*Lifetime*/)
	{
		FGpuPoint& Point = Points.emplace_back();
		ToFloat3(Position - View.ViewOrigin, Point.P);
		Point.Color = Color.ToPackedABGR();
		Point.Size = PointSize;
		Point.Pad[0] = Point.Pad[1] = Point.Pad[2] = 0.0f;
	}

	void FD3D11DebugDrawRenderer::RenderLine(const FVector& A, const FVector& B, const FColor& Color, const float LineThickness, const float /*Lifetime*/)
	{
		AddLine(Lines, A, B, Color, LineThickness);
	}

	void FD3D11DebugDrawRenderer::RenderArrow(const FVector& A, const FVector& B, const float ArrowSize, const FColor& Color, const float LineThickness,
		const float /*Lifetime*/)
	{
		// The head of UE's line batcher arrow (DrawDirectionalArrow, LineBatchComponent.cpp:361-383 [V]): two strokes from the
		// tip, each sqrt(ArrowSize) back along the shaft and sqrt(ArrowSize) out to one side. The side direction is
		// FVector::GetPerpendicularUnit of the shaft, which opens the head in the same plane as UE's (horizontal across the
		// shaft, or across world X for a mostly vertical shaft).
		const FVector Along = (B - A).GetSafeNormal();
		const FVector Across = Along.GetPerpendicularUnit();
		const double HeadLeg = std::sqrt(static_cast<double>(ArrowSize > 0.0f ? ArrowSize : 0.0f));
		const FVector HeadBase = B - Along * HeadLeg;
		AddLine(Lines, A, B, Color, LineThickness);
		AddLine(Lines, B, HeadBase + Across * HeadLeg, Color, LineThickness);
		AddLine(Lines, B, HeadBase - Across * HeadLeg, Color, LineThickness);
	}

	void FD3D11DebugDrawRenderer::RenderCircle(const FVector& Center, const FMatrix& Axes, const float Radius, const FColor& Color, const float LineThickness,
		const float /*Lifetime*/)
	{
		// The unit circle lies in its local Y-Z plane; Axes' Y and Z span the circle (DrawDebugCircle with a matrix).
		AddInstance(Mesh_Circle, Center, Axes.GetUnitAxis(EAxis::X), Axes.GetUnitAxis(EAxis::Y), Axes.GetUnitAxis(EAxis::Z), FVector(Radius), 0.0, Color, LineThickness);
	}

	void FD3D11DebugDrawRenderer::RenderSphere(const FVector& Center, const float Radius, const FColor& Color, const float LineThickness, const float /*Lifetime*/)
	{
		AddInstance(Mesh_Sphere, Center, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0), FVector(Radius), 0.0, Color, LineThickness);
	}

	void FD3D11DebugDrawRenderer::RenderCapsule(const FVector& Center, const FQuat& Rotation, const float HalfHeight, const float Radius, const FColor& Color,
		const float LineThickness, const float /*Lifetime*/)
	{
		// HalfHeight includes the radius: the cap centres sit HalfHeight - Radius from the centre, at least 1 cm (the clamp of
		// the line batcher's capsule, LineBatchComponent.cpp:588 [V]).
		const double CapCentreOffset = FMath::Max(static_cast<double>(HalfHeight) - static_cast<double>(Radius), 1.0);
		AddInstance(Mesh_Capsule, Center, Rotation.GetAxisX(), Rotation.GetAxisY(), Rotation.GetAxisZ(), FVector(Radius), CapCentreOffset, Color, LineThickness);
	}

	void FD3D11DebugDrawRenderer::RenderBox(const FVector& Position, const FQuat& Rotation, const FVector& Size, const FColor& Color, const float LineThickness,
		const float /*Lifetime*/)
	{
		AddInstance(Mesh_Box, Position, Rotation.GetAxisX(), Rotation.GetAxisY(), Rotation.GetAxisZ(), Size, 0.0, Color, LineThickness);
	}

	void FD3D11DebugDrawRenderer::RenderTriangle(const FVector& A, const FVector& B, const FVector& C, const FColor& Color, const float LineThickness,
		const float /*Lifetime*/)
	{
		AddLine(Lines, A, B, Color, LineThickness);
		AddLine(Lines, B, C, Color, LineThickness);
		AddLine(Lines, C, A, Color, LineThickness);
	}

	void FD3D11DebugDrawRenderer::RenderString(const FVector& TextLocation, const std::string_view Text, const FColor& Color, const float FontScale,
		const bool bDrawShadow, const float /*Lifetime*/)
	{
		FDebugDrawString& String = Strings.emplace_back();
		String.Location = TextLocation;
		String.Text.assign(Text);
		String.Color = Color;
		String.FontScale = FontScale;
		String.bDrawShadow = bDrawShadow;
	}

	void FD3D11DebugDrawRenderer::DrawGrid(const double Cell, const int32 MajorEvery, const double HalfExtent)
	{
		// The editor grid in the XY plane at Z = 0 (02 11.1 pass 2). Minor and major lines in greys above the clear colour,
		// the X and Y axes tinted red and green.
		const FColor Minor(58, 60, 66);
		const FColor Major(84, 86, 94);
		const int32 Half = static_cast<int32>(std::floor(HalfExtent / Cell + 0.5));
		for (int32 Index = -Half; Index <= Half; ++Index)
		{
			const double Offset = Cell * Index;
			const bool bMajor = MajorEvery > 0 && Index % MajorEvery == 0;
			const FColor AlongX = Index == 0 ? FColor(140, 60, 60) : (bMajor ? Major : Minor);   // the line y = Offset runs along X
			const FColor AlongY = Index == 0 ? FColor(60, 140, 60) : (bMajor ? Major : Minor);
			AddLine(GridLines, FVector(-HalfExtent, Offset, 0.0), FVector(HalfExtent, Offset, 0.0), AlongX, 0.0f);
			AddLine(GridLines, FVector(Offset, -HalfExtent, 0.0), FVector(Offset, HalfExtent, 0.0), AlongY, 0.0f);
		}
	}

	void FD3D11DebugDrawRenderer::Flush(const bool bSeeThrough)
	{
		ID3D11Device* Device = RHI.GetDevice();
		ID3D11DeviceContext* Context = RHI.GetContext();
		// Statistics.
		Stats.NumBoxes = static_cast<int32>(Instances[Mesh_Box].size());
		Stats.NumSpheres = static_cast<int32>(Instances[Mesh_Sphere].size());
		Stats.NumCapsules = static_cast<int32>(Instances[Mesh_Capsule].size());
		Stats.NumCircles = static_cast<int32>(Instances[Mesh_Circle].size());
		Stats.NumLines = static_cast<int32>(Lines.size());
		Stats.NumPoints = static_cast<int32>(Points.size());
		Stats.NumStrings = static_cast<int32>(Strings.size());
		Stats.NumSegments = static_cast<int64>(Lines.size());
		for (int32 Mesh = 0; Mesh < Mesh_Count; ++Mesh)
		{
			Stats.NumSegments += static_cast<int64>(Instances[Mesh].size()) * Impl->MeshSegments[Mesh];
		}

		// One upload per buffer: instances of every mesh back to back, grid lines then debug lines.
		uint32 InstanceFirst[Mesh_Count] = {};
		Impl->AllInstances.clear();
		for (int32 Mesh = 0; Mesh < Mesh_Count; ++Mesh)
		{
			InstanceFirst[Mesh] = static_cast<uint32>(Impl->AllInstances.size());
			Impl->AllInstances.insert(Impl->AllInstances.end(), Instances[Mesh].begin(), Instances[Mesh].end());
		}
		Impl->AllLines.clear();
		Impl->AllLines.insert(Impl->AllLines.end(), GridLines.begin(), GridLines.end());
		Impl->AllLines.insert(Impl->AllLines.end(), Lines.begin(), Lines.end());
		Impl->InstanceBuffer.Upload(Device, Context, Impl->AllInstances.data(), static_cast<uint32>(Impl->AllInstances.size()), sizeof(FGpuInstance), Stats.UploadBytes);
		Impl->LineBuffer.Upload(Device, Context, Impl->AllLines.data(), static_cast<uint32>(Impl->AllLines.size()), sizeof(FGpuLine), Stats.UploadBytes);
		Impl->PointBuffer.Upload(Device, Context, Points.data(), static_cast<uint32>(Points.size()), sizeof(FGpuPoint), Stats.UploadBytes);

		// View constants.
		{
			D3D11_MAPPED_SUBRESOURCE Mapped = {};
			if (SUCCEEDED(Context->Map(Impl->ViewConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
			{
				FViewConstants Constants = {};
				std::memcpy(Constants.ViewProj, View.ViewProjRelative, sizeof(Constants.ViewProj));
				Constants.Viewport[0] = static_cast<float>(View.Width);
				Constants.Viewport[1] = static_cast<float>(View.Height);
				Constants.NearW = static_cast<float>(View.NearClipPlane);
				Constants.PixelsPerUnitAtW1 = static_cast<float>(View.ProjScaleX * 0.5 * static_cast<double>(View.Width));
				std::memcpy(Mapped.pData, &Constants, sizeof(Constants));
				Context->Unmap(Impl->ViewConstants.Get(), 0);
			}
		}
		const auto SetDrawConstants = [&](const uint32 SegBase, const uint32 ItemBase)
		{
			D3D11_MAPPED_SUBRESOURCE Mapped = {};
			if (SUCCEEDED(Context->Map(Impl->DrawConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
			{
				const FDrawConstants Constants = { SegBase, ItemBase, { 0, 0 } };
				std::memcpy(Mapped.pData, &Constants, sizeof(Constants));
				Context->Unmap(Impl->DrawConstants.Get(), 0);
			}
		};

		Context->IASetInputLayout(nullptr);
		Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		Context->RSSetState(Impl->Rasterizer.Get());
		const float BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		Context->OMSetBlendState(Impl->Opaque.Get(), BlendFactor, 0xFFFFFFFFu);
		ID3D11Buffer* ConstantBuffers[2] = { Impl->ViewConstants.Get(), Impl->DrawConstants.Get() };
		Context->VSSetConstantBuffers(0, 2, ConstantBuffers);
		Context->PSSetShader(Impl->PSMain.Get(), nullptr, 0);
		ID3D11ShaderResourceView* Resources[4] = { Impl->SegmentsSrv.Get(), Impl->InstanceBuffer.Srv.Get(), Impl->LineBuffer.Srv.Get(), Impl->PointBuffer.Srv.Get() };
		Context->VSSetShaderResources(0, 4, Resources);

		const uint32 NumGrid = static_cast<uint32>(GridLines.size());
		const uint32 NumDebugLines = static_cast<uint32>(Lines.size());
		// Pass 2: the grid, depth tested.
		if (NumGrid > 0)
		{
			Context->OMSetDepthStencilState(Impl->DepthTest.Get(), 0);
			Context->VSSetShader(Impl->VSLines.Get(), nullptr, 0);
			SetDrawConstants(0, 0);
			Context->Draw(NumGrid * 6u, 0);
		}
		// Pass 3: the debug lines (twice with SeeThrough: Always, then NearOrEqual).
		const auto DrawLines = [&]()
		{
			for (int32 Mesh = 0; Mesh < Mesh_Count; ++Mesh)
			{
				const uint32 Count = static_cast<uint32>(Instances[Mesh].size());
				if (Count == 0)
				{
					continue;
				}
				Context->VSSetShader(Impl->VSInstanced.Get(), nullptr, 0);
				SetDrawConstants(Impl->MeshFirst[Mesh], InstanceFirst[Mesh]);
				Context->DrawInstanced(Impl->MeshSegments[Mesh] * 6u, Count, 0, 0);
			}
			if (NumDebugLines > 0)
			{
				Context->VSSetShader(Impl->VSLines.Get(), nullptr, 0);
				SetDrawConstants(0, NumGrid);
				Context->Draw(NumDebugLines * 6u, 0);
			}
		};
		if (bSeeThrough)
		{
			Context->OMSetDepthStencilState(Impl->DepthAlways.Get(), 0);
			DrawLines();
		}
		Context->OMSetDepthStencilState(Impl->DepthTest.Get(), 0);
		DrawLines();
		// Pass 4: points.
		if (!Points.empty())
		{
			Context->OMSetDepthStencilState(bSeeThrough ? Impl->DepthAlways.Get() : Impl->DepthTest.Get(), 0);
			Context->VSSetShader(Impl->VSPoints.Get(), nullptr, 0);
			SetDrawConstants(0, 0);
			Context->Draw(static_cast<uint32>(Points.size()) * 6u, 0);
		}
		ID3D11ShaderResourceView* NullResources[4] = {};
		Context->VSSetShaderResources(0, 4, NullResources);
	}
}
