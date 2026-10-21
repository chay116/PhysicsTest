# PT::PhysicsDebugDraw - the Chaos-style debug draw pipeline without any renderer (02_Architecture 4.5, 11.1, A.11; M4):
# FDebugDrawQueue (Chaos::FDebugDrawQueue signatures, budget and region), FDebugDrawSettings / FDebugDrawFlags and their
# p.Chaos.* console variables, the drawers in FPBDRigidsSolver::PostTickDebugDraw order, FIslandBuilder, the unit wire
# meshes, IDebugDrawRenderer (IChaosDDRenderer) and VisitCommands. No D3D: PhysicsTestAutomation links it in every preset.
pt_add_module(PtPhysicsDebugDraw STATIC ALIAS PT::PhysicsDebugDraw
	SOURCES
		Public/DebugDrawQueue.h
		Public/DebugDrawRenderer.h
		Public/DebugDrawSettings.h
		Public/IslandBuilder.h
		Public/PhysicsDebugDraw.h
		Public/WireMeshes.h
		Private/DebugDrawQueue.cpp
		Private/DebugDrawRenderer.cpp
		Private/DebugDrawSettings.cpp
		Private/IslandBuilder.cpp
		Private/PhysicsDebugDraw.cpp
		Private/WireMeshes.cpp
	PUBLIC_DEPS PT::PhysicsCore)
