// Headless D3D11 prototype of a Chaos-debug-draw-style renderer.
// - instanced wire primitives (box 12 / sphere 128 / capsule 68 segments, UE LineBatchComponent topology)
// - thick lines expanded in the VS from StructuredBuffers (no GS), reversed-Z, 4x MSAA
// - ImGui + ImPlot overlay rendered into the same offscreen target, text labels via ImGui draw list
// - resolve + readback + PNG (screenshot path that also works without a window)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <random>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "implot.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define HR(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { std::printf("FAILED %s -> 0x%08lx (line %d)\n", #x, (unsigned long)hr_, __LINE__); std::exit(1); } } while (0)
template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// ---------------- minimal UE-like math (float, Z-up, left-handed) ----------------
struct FVector { float X, Y, Z; };
static FVector operator-(FVector a, FVector b) { return {a.X - b.X, a.Y - b.Y, a.Z - b.Z}; }
static float Dot(FVector a, FVector b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
static FVector Cross(FVector a, FVector b) { return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X}; }
static FVector Norm(FVector a) { float l = std::sqrt(Dot(a, a)); return {a.X / l, a.Y / l, a.Z / l}; }
struct FQuat { float X, Y, Z, W; };
struct M44 { float m[4][4]; };  // row-major storage, column-vector convention: clip = M * v
static M44 Mul(const M44& a, const M44& b) { M44 r{}; for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) for (int k = 0; k < 4; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j]; return r; }
static M44 LookAtLH(FVector eye, FVector at, FVector up) {
  FVector z = Norm(at - eye), x = Norm(Cross(up, z)), y = Cross(z, x);
  return {{{x.X, x.Y, x.Z, -Dot(x, eye)}, {y.X, y.Y, y.Z, -Dot(y, eye)}, {z.X, z.Y, z.Z, -Dot(z, eye)}, {0, 0, 0, 1}}};
}
static M44 PerspectiveReversedZ(float fovY, float aspect, float n, float f) {  // near -> 1, far -> 0
  float ys = 1.0f / std::tan(fovY * 0.5f), xs = ys / aspect, A = n / (n - f), B = -f * n / (n - f);
  return {{{xs, 0, 0, 0}, {0, ys, 0, 0}, {0, 0, A, B}, {0, 0, 1, 0}}};
}

// ---------------- unit wire meshes (segment endpoint pairs, w = capsule cap sign) ----------------
struct V4 { float x, y, z, w; };
static void Seg(std::vector<V4>& o, V4 a, V4 b) { o.push_back(a); o.push_back(b); }
static std::vector<V4> MakeBox() {  // +-1 cube, 12 segments (ULineBatchComponent::DrawBox)
  std::vector<V4> o; float s[2] = {-1, 1};
  for (float a : s) for (float b : s) { Seg(o, {-1, a, b, 0}, {1, a, b, 0}); Seg(o, {a, -1, b, 0}, {a, 1, b, 0}); Seg(o, {a, b, -1, 0}, {a, b, 1, 0}); }
  return o;
}
static std::vector<V4> MakeSphere(int seg) {  // ULineBatchComponent::DrawSphere topology: 2*seg*seg segments
  std::vector<V4> o; const float inc = 6.2831853f / seg; float lat = inc, sy1 = 0, cy1 = 1;
  for (int iy = 0; iy < seg; ++iy, lat += inc) {
    float sy2 = std::sin(lat), cy2 = std::cos(lat); V4 v1{sy1, 0, cy1, 0}, v3{sy2, 0, cy2, 0}; float lon = inc;
    for (int ix = 0; ix < seg; ++ix, lon += inc) {
      float sx = std::sin(lon), cx = std::cos(lon); V4 v2{cx * sy1, sx * sy1, cy1, 0}, v4{cx * sy2, sx * sy2, cy2, 0};
      Seg(o, v1, v2); Seg(o, v1, v3); v1 = v2; v3 = v4;
    }
    sy1 = sy2; cy1 = cy2;
  }
  return o;
}
static std::vector<V4> MakeCapsule(int sides) {  // ULineBatchComponent::DrawCapsule: 2 circles + 4 half circles + 4 lines
  std::vector<V4> o; const float inc = 6.2831853f / sides;
  for (float cap : {1.0f, -1.0f}) {
    for (int i = 0; i < sides; ++i) Seg(o, {std::cos(i * inc), std::sin(i * inc), 0, cap}, {std::cos((i + 1) * inc), std::sin((i + 1) * inc), 0, cap});
    for (int axis = 0; axis < 2; ++axis)
      for (int i = 0; i < sides / 2; ++i) {
        float a0 = i * inc, a1 = (i + 1) * inc; auto P = [&](float a) { float h = std::cos(a), v = std::sin(a) * cap; return axis ? V4{0, h, v, cap} : V4{h, 0, v, cap}; };
        Seg(o, P(a0), P(a1));
      }
  }
  for (V4 d : {V4{1, 0, 0, 0}, V4{-1, 0, 0, 0}, V4{0, 1, 0, 0}, V4{0, -1, 0, 0}}) Seg(o, {d.x, d.y, 0, 1}, {d.x, d.y, 0, -1});
  return o;
}

// per-instance data (64 bytes): rigid 3x4 world rows, scale/halfheight params, color, thickness
struct alignas(16) FWireInstance { float R0[4], R1[4], R2[4]; float Params[4]; };  // Params: sx, sy, sz, halfHeight
struct FInstanceExtra { uint32_t Color; float Thickness; uint32_t pad[2]; };
struct GpuInstance { FWireInstance W; FInstanceExtra E; };  // 80 bytes
static_assert(sizeof(GpuInstance) == 80, "");
struct alignas(16) ViewCB { M44 ViewProj; float Viewport[2]; float NearW; float pad; };

static const char* kHlsl = R"(
cbuffer View : register(b0) { row_major float4x4 ViewProj; float2 Viewport; float NearW; float _p; };
struct Inst { float4 R0, R1, R2, Params; uint Color; float Thickness; uint2 _pad; };
StructuredBuffer<float4> Segs  : register(t0);
StructuredBuffer<Inst>   Insts : register(t1);
struct VSOut { float4 Pos : SV_Position; float4 Col : COLOR0; };
float4 Unpack(uint c) { return float4(c & 255, (c >> 8) & 255, (c >> 16) & 255, c >> 24) / 255.0; }
float3 Xf(Inst I, float4 v) { float4 l = float4(v.xyz * I.Params.xyz, 1); l.z += v.w * I.Params.w; return float3(dot(I.R0, l), dot(I.R1, l), dot(I.R2, l)); }
VSOut Expand(float3 a, float3 b, uint corner, float thick, float4 col) {
  float4 ca = mul(ViewProj, float4(a, 1)), cb = mul(ViewProj, float4(b, 1));
  if (ca.w < NearW && cb.w < NearW) { VSOut o; o.Pos = float4(0, 0, -2, 1); o.Col = col; return o; }  // fully behind: culled
  if (ca.w < NearW) ca = lerp(ca, cb, (NearW - ca.w) / (cb.w - ca.w));
  if (cb.w < NearW) cb = lerp(cb, ca, (NearW - cb.w) / (ca.w - cb.w));
  float2 sa = ca.xy / ca.w * Viewport * 0.5, sb = cb.xy / cb.w * Viewport * 0.5;
  float2 d = sb - sa; d = dot(d, d) > 1e-8 ? normalize(d) : float2(1, 0); float2 n = float2(-d.y, d.x);
  static const float2 kC[6] = { float2(0,-1), float2(0,1), float2(1,1), float2(0,-1), float2(1,1), float2(1,-1) };
  float2 c = kC[corner]; float4 p = c.x > 0.5 ? cb : ca;
  float2 offPx = (n * c.y + d * (c.x > 0.5 ? 1 : -1)) * thick * 0.5;   // side offset + half-thickness end cap
  p.xy += offPx / (Viewport * 0.5) * p.w;
  VSOut o; o.Pos = p; o.Col = col; return o;
}
VSOut VSWireThick(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
  Inst I = Insts[iid]; uint s = vid / 6;
  return Expand(Xf(I, Segs[s * 2]), Xf(I, Segs[s * 2 + 1]), vid % 6, I.Thickness, Unpack(I.Color));
}
VSOut VSWireHw(uint vid : SV_VertexID, uint iid : SV_InstanceID) {  // 1px hardware LINELIST
  Inst I = Insts[iid]; VSOut o; o.Pos = mul(ViewProj, float4(Xf(I, Segs[vid]), 1)); o.Col = Unpack(I.Color); return o;
}
float4 PSMain(VSOut i) : SV_Target { return i.Col; }
)";

static ID3DBlob* Compile(const char* entry, const char* target) {
  ID3DBlob *code = nullptr, *err = nullptr;
  HRESULT hr = D3DCompile(kHlsl, std::strlen(kHlsl), "embedded.hlsl", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
  if (FAILED(hr)) { std::printf("HLSL %s: %s\n", entry, err ? (const char*)err->GetBufferPointer() : "?"); std::exit(1); }
  SafeRelease(err); return code;
}
static double Now() { static LARGE_INTEGER f; if (!f.QuadPart) QueryPerformanceFrequency(&f); LARGE_INTEGER c; QueryPerformanceCounter(&c); return double(c.QuadPart) / double(f.QuadPart); }

int main(int argc, char** argv) {
  const int W = 1600, H = 900, MSAA = 4; int N = argc > 1 ? std::atoi(argv[1]) : 10000; const int Frames = 60;
  ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; D3D_FEATURE_LEVEL fl{}; const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0};
  HR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, want, 1, D3D11_SDK_VERSION, &dev, &fl, &ctx));
  { IDXGIDevice* dd; dev->QueryInterface(IID_PPV_ARGS(&dd)); IDXGIAdapter* ad; dd->GetAdapter(&ad); DXGI_ADAPTER_DESC desc; ad->GetDesc(&desc);
    std::printf("adapter: %ls  FL=0x%x\n", desc.Description, fl); ad->Release(); dd->Release(); }
  UINT q = 0; HR(dev->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, MSAA, &q)); std::printf("MSAA %dx quality levels: %u\n", MSAA, q);

  // offscreen targets
  auto Tex = [&](DXGI_FORMAT fmt, UINT bind, UINT samples, D3D11_USAGE usage = D3D11_USAGE_DEFAULT, UINT cpu = 0) {
    D3D11_TEXTURE2D_DESC d{}; d.Width = W; d.Height = H; d.MipLevels = 1; d.ArraySize = 1; d.Format = fmt; d.SampleDesc.Count = samples; d.Usage = usage; d.BindFlags = bind; d.CPUAccessFlags = cpu;
    ID3D11Texture2D* t; HR(dev->CreateTexture2D(&d, nullptr, &t)); return t; };
  ID3D11Texture2D* colorMS = Tex(DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, MSAA);
  ID3D11Texture2D* depthMS = Tex(DXGI_FORMAT_D32_FLOAT, D3D11_BIND_DEPTH_STENCIL, MSAA);
  ID3D11Texture2D* resolved = Tex(DXGI_FORMAT_R8G8B8A8_UNORM, 0, 1);
  ID3D11Texture2D* staging = Tex(DXGI_FORMAT_R8G8B8A8_UNORM, 0, 1, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
  ID3D11RenderTargetView* rtv; HR(dev->CreateRenderTargetView(colorMS, nullptr, &rtv));
  ID3D11DepthStencilView* dsv; HR(dev->CreateDepthStencilView(depthMS, nullptr, &dsv));

  // pipeline
  ID3DBlob *vsT = Compile("VSWireThick", "vs_5_0"), *vsH = Compile("VSWireHw", "vs_5_0"), *ps = Compile("PSMain", "ps_5_0");
  ID3D11VertexShader *vsThick, *vsHw; ID3D11PixelShader* psMain;
  HR(dev->CreateVertexShader(vsT->GetBufferPointer(), vsT->GetBufferSize(), nullptr, &vsThick));
  HR(dev->CreateVertexShader(vsH->GetBufferPointer(), vsH->GetBufferSize(), nullptr, &vsHw));
  HR(dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &psMain));
  D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; rd.MultisampleEnable = TRUE;
  ID3D11RasterizerState* rs; HR(dev->CreateRasterizerState(&rd, &rs));
  D3D11_DEPTH_STENCIL_DESC dsd{}; dsd.DepthEnable = TRUE; dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dsd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;  // reversed-Z
  ID3D11DepthStencilState* dss; HR(dev->CreateDepthStencilState(&dsd, &dss));

  // static unit meshes in one structured buffer
  std::vector<V4> box = MakeBox(), sph = MakeSphere(8), cap = MakeCapsule(16), all;
  const UINT boxOff = 0, sphOff = UINT(box.size()), capOff = UINT(box.size() + sph.size());
  all.insert(all.end(), box.begin(), box.end()); all.insert(all.end(), sph.begin(), sph.end()); all.insert(all.end(), cap.begin(), cap.end());
  std::printf("segments per shape: box=%zu sphere(8)=%zu capsule(16)=%zu\n", box.size() / 2, sph.size() / 2, cap.size() / 2);
  auto MakeSB = [&](UINT stride, UINT count, const void* init, bool dynamic, ID3D11Buffer** buf, ID3D11ShaderResourceView** srv) {
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = stride * count; bd.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = stride;
    D3D11_SUBRESOURCE_DATA sd{init}; HR(dev->CreateBuffer(&bd, init ? &sd : nullptr, buf)); HR(dev->CreateShaderResourceView(*buf, nullptr, srv)); };
  ID3D11Buffer *segBuf, *instBuf; ID3D11ShaderResourceView *segSrv, *instSrv;
  MakeSB(sizeof(V4), UINT(all.size()), all.data(), false, &segBuf, &segSrv);
  MakeSB(sizeof(GpuInstance), UINT(3 * N), nullptr, true, &instBuf, &instSrv);
  D3D11_BUFFER_DESC cbd{}; cbd.ByteWidth = sizeof(ViewCB); cbd.Usage = D3D11_USAGE_DYNAMIC; cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  ID3D11Buffer* cb; HR(dev->CreateBuffer(&cbd, nullptr, &cb));

  // scene: N boxes, N spheres, N capsules on a grid (cm, Z-up), Chaos default state colors (ChaosDebugDraw.cpp:389-393)
  const uint32_t kDynamic = 0xFF00FFFF, kSleeping = 0xFF808080, kKinematic = 0xFFFF8000, kStatic = 0xFF0000FF;  // ABGR packed RGBA8
  std::mt19937 rng(1); std::uniform_real_distribution<float> U(-1, 1);
  int side = int(std::ceil(std::cbrt(3.0 * N)));
  auto Fill = [&](GpuInstance* dst) {
    for (int i = 0; i < 3 * N; ++i) {
      int t = i % 3; float x = float(i % side) * 150.f, y = float((i / side) % side) * 150.f, z = float(i / (side * side)) * 150.f + 100.f;
      FQuat qq{U(rng), U(rng), U(rng), U(rng)}; float l = std::sqrt(qq.X * qq.X + qq.Y * qq.Y + qq.Z * qq.Z + qq.W * qq.W); qq = {qq.X / l, qq.Y / l, qq.Z / l, qq.W / l};
      float xx = qq.X * qq.X, yy = qq.Y * qq.Y, zz = qq.Z * qq.Z, xy = qq.X * qq.Y, xz = qq.X * qq.Z, yz = qq.Y * qq.Z, wx = qq.W * qq.X, wy = qq.W * qq.Y, wz = qq.W * qq.Z;
      GpuInstance& g = dst[i];
      g.W = {{1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy), x}, {2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx), y}, {2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy), z},
             {t == 0 ? 40.f : 35.f, t == 0 ? 25.f : 35.f, t == 0 ? 20.f : 35.f, t == 2 ? 30.f : 0.f}};
      g.E = {(i % 7 == 0) ? kSleeping : (i % 11 == 0) ? kKinematic : (i % 13 == 0) ? kStatic : kDynamic, 1.5f, {0, 0}};
    }
  };
  // reorder so each shape type is contiguous: [boxes][spheres][capsules]
  std::vector<GpuInstance> scratch(3 * N), sorted(3 * N);

  float extent = side * 150.f; FVector eye{-extent * 0.6f, -extent * 0.6f, extent * 0.9f}, at{extent * 0.5f, extent * 0.5f, extent * 0.3f};
  M44 vp = Mul(PerspectiveReversedZ(60.f * 3.14159265f / 180.f, float(W) / H, 10.f, 1e6f), LookAtLH(eye, at, {0, 0, 1}));

  D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}; ID3D11Query *qDis, *qT0, *qT1; HR(dev->CreateQuery(&qd, &qDis)); qd.Query = D3D11_QUERY_TIMESTAMP; HR(dev->CreateQuery(&qd, &qT0)); HR(dev->CreateQuery(&qd, &qT1));
  auto GpuMs = [&]() { D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj; while (ctx->GetData(qDis, &dj, sizeof(dj), 0) != S_OK) {} UINT64 a, b; while (ctx->GetData(qT0, &a, sizeof(a), 0) != S_OK) {} while (ctx->GetData(qT1, &b, sizeof(b), 0) != S_OK) {}
    return dj.Disjoint ? -1.0 : double(b - a) * 1000.0 / double(dj.Frequency); };

  D3D11_VIEWPORT vpd{0, 0, float(W), float(H), 0, 1};
  // Per-type ranges are bound as SRV sub-ranges (FirstElement) so SV_VertexID/SV_InstanceID start at 0 for every draw.
  ID3D11ShaderResourceView* instRange[3]; ID3D11ShaderResourceView* segRange[3];
  for (int t = 0; t < 3; ++t) {
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = DXGI_FORMAT_UNKNOWN; sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sv.Buffer.FirstElement = UINT(t * N); sv.Buffer.NumElements = UINT(N); HR(dev->CreateShaderResourceView(instBuf, &sv, &instRange[t]));
    const UINT offs[3] = {boxOff, sphOff, capOff}; const UINT cnt[3] = {UINT(box.size()), UINT(sph.size()), UINT(cap.size())};
    sv.Buffer.FirstElement = offs[t]; sv.Buffer.NumElements = cnt[t]; HR(dev->CreateShaderResourceView(segBuf, &sv, &segRange[t]));
  }
  const UINT segCount[3] = {UINT(box.size() / 2), UINT(sph.size() / 2), UINT(cap.size() / 2)};
  auto Draw = [&](bool thick) {
    float clear[4] = {0.08f, 0.08f, 0.09f, 1}; ctx->ClearRenderTargetView(rtv, clear); ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 0.0f, 0);
    ctx->OMSetRenderTargets(1, &rtv, dsv); ctx->OMSetDepthStencilState(dss, 0); ctx->RSSetState(rs); ctx->RSSetViewports(1, &vpd);
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(thick ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    ctx->VSSetShader(thick ? vsThick : vsHw, nullptr, 0); ctx->PSSetShader(psMain, nullptr, 0); ctx->VSSetConstantBuffers(0, 1, &cb);
    for (int t = 0; t < 3; ++t) {
      ID3D11ShaderResourceView* srvs[2] = {segRange[t], instRange[t]}; ctx->VSSetShaderResources(0, 2, srvs);
      ctx->DrawInstanced(thick ? segCount[t] * 6 : segCount[t] * 2, UINT(N), 0, 0);
    }
  };

  { D3D11_MAPPED_SUBRESOURCE m; HR(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)); ViewCB v{vp, {float(W), float(H)}, 10.f, 0}; std::memcpy(m.pData, &v, sizeof(v)); ctx->Unmap(cb, 0); }

  for (int mode = 0; mode < 2; ++mode) {
    bool thick = mode == 1; double cpuSum = 0, gpuSum = 0; int gpuN = 0;
    for (int f = 0; f < Frames; ++f) {
      double t0 = Now();
      Fill(scratch.data());
      for (int i = 0; i < 3 * N; ++i) sorted[(i % 3) * N + i / 3] = scratch[i];  // N per type (3N total, i%3 balanced)
      D3D11_MAPPED_SUBRESOURCE m; HR(ctx->Map(instBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)); std::memcpy(m.pData, sorted.data(), sorted.size() * sizeof(GpuInstance)); ctx->Unmap(instBuf, 0);
      double t1 = Now(); cpuSum += t1 - t0;
      ctx->Begin(qDis); ctx->End(qT0); Draw(thick); ctx->End(qT1); ctx->End(qDis);
      double g = GpuMs(); if (f >= 5 && g >= 0) { gpuSum += g; ++gpuN; }
    }
    size_t segs = size_t(N) * (segCount[0] + segCount[1] + segCount[2]);
    std::printf("[%s] shapes=%d segments/frame=%zu  instance upload=%zu KB  CPU fill+upload=%.3f ms  GPU draw=%.3f ms\n", thick ? "thick 1.5px (VS-expanded quads)" : "hw 1px LINELIST",
                3 * N, segs, sorted.size() * sizeof(GpuInstance) / 1024, cpuSum / Frames * 1000.0, gpuN ? gpuSum / gpuN : -1.0);
  }

  // ImGui + ImPlot overlay into the same offscreen MSAA target (no platform backend needed)
  IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImPlot::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); io.DisplaySize = ImVec2(float(W), float(H)); io.DeltaTime = 1.0f / 60.0f; io.IniFilename = nullptr;
  ImGui_ImplDX11_Init(dev, ctx);
  static float xs[120], ys[3][120]; for (int i = 0; i < 120; ++i) { xs[i] = float(i); ys[0][i] = 2.f + std::sin(i * 0.1f); ys[1][i] = 3.f + std::cos(i * 0.07f); ys[2][i] = 1.5f + 0.5f * std::sin(i * 0.2f); }
  for (int pass = 0; pass < 2; ++pass) {  // 2 frames so the font atlas/texture upload path is exercised
    ImGui_ImplDX11_NewFrame(); ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10, 10)); ImGui::SetNextWindowSize(ImVec2(520, 300));
    ImGui::Begin("Step time (ms)");
    if (ImPlot::BeginPlot("##step", ImVec2(-1, -1))) { ImPlot::PlotLine("Jolt", xs, ys[0], 120); ImPlot::PlotLine("PhysX", xs, ys[1], 120); ImPlot::PlotLine("Bullet", xs, ys[2], 120); ImPlot::EndPlot(); }
    ImGui::End();
    // 3D text labels (DrawDebugString equivalent): project on CPU, draw with ImGui foreground list
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (int i = 0; i < 3 * N && i < 300; i += 7) {
      const GpuInstance& g = sorted[i]; float p[4] = {g.W.R0[3], g.W.R1[3], g.W.R2[3], 1}, c[4] = {};
      for (int r = 0; r < 4; ++r) for (int k = 0; k < 4; ++k) c[r] += vp.m[r][k] * p[k];
      if (c[3] <= 10.f) continue; float sx = (c[0] / c[3] * 0.5f + 0.5f) * W, sy = (1 - (c[1] / c[3] * 0.5f + 0.5f)) * H;
      char buf[32]; std::snprintf(buf, sizeof buf, "#%d", i); dl->AddText(ImVec2(sx, sy), IM_COL32(255, 255, 255, 255), buf);
    }
    ImGui::Render();
    Draw(true);  // re-draw scene under the overlay
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  }
  // resolve + readback + PNG
  double r0 = Now();
  ctx->ResolveSubresource(resolved, 0, colorMS, 0, DXGI_FORMAT_R8G8B8A8_UNORM); ctx->CopyResource(staging, resolved);
  D3D11_MAPPED_SUBRESOURCE m; HR(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m));
  std::vector<uint8_t> px(size_t(W) * H * 4); for (int y = 0; y < H; ++y) std::memcpy(&px[size_t(y) * W * 4], (uint8_t*)m.pData + size_t(y) * m.RowPitch, size_t(W) * 4);
  ctx->Unmap(staging, 0); double r1 = Now();
  int ok = stbi_write_png("proto_render.png", W, H, 4, px.data(), W * 4); double r2 = Now();
  std::printf("screenshot: resolve+readback %.2f ms, PNG encode %.1f ms -> %s\n", (r1 - r0) * 1000, (r2 - r1) * 1000, ok ? "proto_render.png" : "FAILED");
  ImGui_ImplDX11_Shutdown(); ImPlot::DestroyContext(); ImGui::DestroyContext();
  return 0;
}
