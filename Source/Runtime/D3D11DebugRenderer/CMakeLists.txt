# PT::D3D11DebugRenderer - the viewer's D3D11 side (02_Architecture 4.5, 11.1, A.11; M4): FD3D11RHI (device, 4x MSAA scene
# targets, swap chain or headless output, PNG capture, GPU timestamps), FSceneView (UE view basis, hFOV 90 on the major
# axis, reversed-Z infinite far) and FD3D11DebugDrawRenderer (IDebugDrawRenderer: instanced unit wires, VS-expanded lines,
# SeeThrough passes). Ported from prototypes/render_d3d11. Shaders are compiled at run time (d3dcompiler_47, System32).
# Built only with PT_WITH_VIEWER (dbg, dev); the test preset and the bench exe never see D3D.
pt_add_module(PtD3D11DebugRenderer STATIC ALIAS PT::D3D11DebugRenderer
	SOURCES
		Public/D3D11DebugDrawRenderer.h
		Public/D3D11RHI.h
		Public/SceneView.h
		Private/D3D11DebugDrawRenderer.cpp
		Private/D3D11RHI.cpp
		Private/D3D11RHIPrivate.h
		Private/SceneView.cpp
		Private/Shaders.hlsl.inl
		Private/StbImageWrite.cpp
	PUBLIC_DEPS PT::PhysicsDebugDraw
	PRIVATE_DEPS PT::Stb d3d11 dxgi d3dcompiler)
