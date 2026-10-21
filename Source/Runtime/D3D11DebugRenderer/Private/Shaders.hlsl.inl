// Shaders.hlsl.inl - the debug draw shaders, compiled at run time with d3dcompiler_47 (System32). Included by
// D3D11DebugDrawRenderer.cpp as one raw string. From the verified prototype (proto_render.cpp: VS-expanded quads, no GS),
// with world-space widths, per-draw bases and screen-space points.
R"HLSL(
cbuffer View : register(b0)
{
	row_major float4x4 ViewProj;   // camera-relative world -> clip (reversed-Z, infinite far)
	float2 Viewport;               // pixels
	float NearW;                   // the near plane distance (clip w)
	float PixelsPerUnitAtW1;       // ProjScaleX * Width / 2: pixels per cm at view depth 1 cm
};
cbuffer Draw : register(b1)
{
	uint SegBase;    // first float4 of the unit mesh in Segs
	uint ItemBase;   // first instance / line / point of this draw
	uint2 DrawPad;
};
struct Inst { float4 R0, R1, R2, Params; uint Color; float Thickness; uint2 Pad; };
struct Line { float3 A; uint Color; float3 B; float Thickness; };
struct Pt { float3 P; uint Color; float Size; float3 Pad; };
StructuredBuffer<float4> Segs  : register(t0);
StructuredBuffer<Inst>   Insts : register(t1);
StructuredBuffer<Line>   Lines : register(t2);
StructuredBuffer<Pt>     Points : register(t3);
struct VSOut { float4 Pos : SV_Position; float4 Col : COLOR0; };

float4 Unpack(uint C) { return float4(C & 255, (C >> 8) & 255, (C >> 16) & 255, C >> 24) / 255.0; }

// Unit vertex -> camera-relative world: scale, capsule cap offset along local Z, rotation + translation rows.
float3 Xf(Inst I, float4 V)
{
	float4 L = float4(V.xyz * I.Params.xyz, 1);
	L.z += V.w * I.Params.w;
	return float3(dot(I.R0, L), dot(I.R1, L), dot(I.R2, L));
}

float WidthPixels(float Thickness, float W)
{
	return Thickness > 0 ? max(Thickness * PixelsPerUnitAtW1 / max(W, 1e-3), 1.0) : 1.0;
}

// One corner of the quad of segment A-B: corners 0..5 = two triangles; x = which end, y = which side.
VSOut ExpandSegment(float3 A, float3 B, uint Corner, float Thickness, float4 Col)
{
	VSOut O;
	O.Col = Col;
	float4 CA = mul(ViewProj, float4(A, 1));
	float4 CB = mul(ViewProj, float4(B, 1));
	if (CA.w < NearW && CB.w < NearW)
	{
		O.Pos = float4(0, 0, -2, 1);   // entirely behind the near plane: outside the clip volume
		return O;
	}
	if (CA.w < NearW) CA = lerp(CA, CB, (NearW - CA.w) / (CB.w - CA.w));
	if (CB.w < NearW) CB = lerp(CB, CA, (NearW - CB.w) / (CA.w - CB.w));
	float2 SA = CA.xy / CA.w * Viewport * 0.5;
	float2 SB = CB.xy / CB.w * Viewport * 0.5;
	float2 D = SB - SA;
	D = dot(D, D) > 1e-8 ? normalize(D) : float2(1, 0);
	float2 N = float2(-D.y, D.x);
	static const float2 KCorners[6] = { float2(0, -1), float2(0, 1), float2(1, 1), float2(0, -1), float2(1, 1), float2(1, -1) };
	float2 C = KCorners[Corner];
	bool bEndB = C.x > 0.5;
	float4 P = bEndB ? CB : CA;
	float Px = WidthPixels(Thickness, P.w);
	float2 OffsetPx = (N * C.y + D * (bEndB ? 1 : -1)) * Px * 0.5;   // side offset + half-width end cap
	P.xy += OffsetPx / (Viewport * 0.5) * P.w;
	O.Pos = P;
	return O;
}

VSOut VSInstanced(uint VertexId : SV_VertexID, uint InstanceId : SV_InstanceID)
{
	Inst I = Insts[ItemBase + InstanceId];
	uint S = SegBase + (VertexId / 6) * 2;
	return ExpandSegment(Xf(I, Segs[S]), Xf(I, Segs[S + 1]), VertexId % 6, I.Thickness, Unpack(I.Color));
}

VSOut VSLines(uint VertexId : SV_VertexID)
{
	Line L = Lines[ItemBase + VertexId / 6];
	return ExpandSegment(L.A, L.B, VertexId % 6, L.Thickness, Unpack(L.Color));
}

VSOut VSPoints(uint VertexId : SV_VertexID)
{
	Pt P = Points[ItemBase + VertexId / 6];
	VSOut O;
	O.Col = Unpack(P.Color);
	float4 C = mul(ViewProj, float4(P.P, 1));
	if (C.w < NearW)
	{
		O.Pos = float4(0, 0, -2, 1);
		return O;
	}
	static const float2 KCorners[6] = { float2(-1, -1), float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(1, -1) };
	C.xy += KCorners[VertexId % 6] * (max(P.Size, 1.0) * 0.5) / (Viewport * 0.5) * C.w;
	O.Pos = C;
	return O;
}

float4 PSMain(VSOut I) : SV_Target { return I.Col; }
)HLSL"
