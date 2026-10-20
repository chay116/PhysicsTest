# ImGui.cmake - Dear ImGui v1.92.9b-docking + ImPlot v1.0 (02_Architecture 4.3, D4).
#
#   PT::ImGuiHeaders  INTERFACE  include directories only (always available; used by the third-party include probe)
#   PT::ImGui         STATIC     imgui + Win32/D3D11 backends + implot; in ALL only when PT_WITH_VIEWER
#
# Harness TUs include these headers only between Misc/PtThirdPartyIncludesStart.h and ...End.h (DL03).
# IMGUI_DISABLE_OBSOLETE_FUNCTIONS stays undefined: implot v1.0 breaks with imgui >= 1.92.8 (AddPolyline argument swap).
include_guard(GLOBAL)
include(PtFetch)

pt_fetch_url_pinned(imgui
	URL        https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b-docking.tar.gz
	SHA256     90ded916bd57db2e0e171b6b098940a47c6f5042725dcdc67fb19940ca8bfdcc
	CACHE_FILE imgui-v1.92.9b-docking.tar.gz)
pt_fetch_url_pinned(implot
	URL        https://github.com/epezent/implot/archive/refs/tags/v1.0.tar.gz
	SHA256     e4a9db64eef7bcc604e2a5ea380af124eb97aa3e8a6a96330079c88add0f7e93
	CACHE_FILE implot-v1.0.tar.gz)

add_library(pt_imgui_headers INTERFACE)
add_library(PT::ImGuiHeaders ALIAS pt_imgui_headers)
target_include_directories(pt_imgui_headers SYSTEM INTERFACE
	"${PT_IMGUI_SOURCE_DIR}"
	"${PT_IMGUI_SOURCE_DIR}/backends"
	"${PT_IMPLOT_SOURCE_DIR}")

add_library(pt_imgui STATIC
	"${PT_IMGUI_SOURCE_DIR}/imgui.cpp"
	"${PT_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
	"${PT_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
	"${PT_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
	"${PT_IMGUI_SOURCE_DIR}/imgui_demo.cpp"
	"${PT_IMGUI_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
	"${PT_IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
	"${PT_IMPLOT_SOURCE_DIR}/implot.cpp"
	"${PT_IMPLOT_SOURCE_DIR}/implot_items.cpp"
	"${PT_IMPLOT_SOURCE_DIR}/implot_demo.cpp")
add_library(PT::ImGui ALIAS pt_imgui)
target_link_libraries(pt_imgui PUBLIC pt_imgui_headers d3d11 dxgi d3dcompiler)
target_compile_features(pt_imgui PRIVATE cxx_std_17)
set_target_properties(pt_imgui PROPERTIES FOLDER "ThirdParty/ImGui")
if(NOT PT_WITH_VIEWER)
	set_target_properties(pt_imgui PROPERTIES EXCLUDE_FROM_ALL ON)
endif()
