# SmallDeps.cmake - header-only dependencies pinned by URL + SHA-256 (02_Architecture 4.3).
#
#   PT::Stb      stb @ a7d4c217 (stb_image_write for viewer captures, M4)
#   PT::Json     nlohmann/json v3.12.0 (single_include)
#   PT::Doctest  doctest v2.5.3 (PhysicsTestAutomation)
#
# Harness TUs include doctest and nlohmann/json only between Misc/PtThirdPartyIncludesStart.h and ...End.h:
# doctest declares a member function check() (doctest.h:1647) that collides with the harness check() macro.
include_guard(GLOBAL)
include(PtFetch)

pt_fetch_url_pinned(stb
	URL        https://github.com/nothings/stb/archive/a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d.tar.gz
	SHA256     ae498e10fadeba0c2e7aa86c71cc9989c4d895201cd26b8b72061c76cfde4edf
	CACHE_FILE stb-a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d.tar.gz)
pt_fetch_url_pinned(nlohmann_json
	URL        https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
	SHA256     42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
	CACHE_FILE json-v3.12.0.tar.xz)
pt_fetch_url_pinned(doctest
	URL        https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
	SHA256     174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
	CACHE_FILE doctest-v2.5.3.tar.gz)

add_library(pt_stb INTERFACE)
add_library(PT::Stb ALIAS pt_stb)
target_include_directories(pt_stb SYSTEM INTERFACE "${PT_STB_SOURCE_DIR}")

add_library(pt_json INTERFACE)
add_library(PT::Json ALIAS pt_json)
target_include_directories(pt_json SYSTEM INTERFACE "${PT_NLOHMANN_JSON_SOURCE_DIR}/single_include")

add_library(pt_doctest INTERFACE)
add_library(PT::Doctest ALIAS pt_doctest)
target_include_directories(pt_doctest SYSTEM INTERFACE "${PT_DOCTEST_SOURCE_DIR}")

# Tracy v0.14.1 is used only by the prof preset (M10). Pin recorded now (verified 2026-10-21 against the GitHub
# archive and the cached tarball): https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
#   SHA256 bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1
