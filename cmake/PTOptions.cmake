# PTOptions.cmake - user-facing options, SDK pins and repository URLs (02_Architecture 4.3, D3, DL54).
#
# SDK sources are always cloned into the build tree (out/<preset>/_deps). PT_SDK_MIRROR (cache or environment)
# points at a local READ-ONLY mirror that holds JoltPhysics/, PhysX/ and bullet3/ at the pinned SHAs; when it is
# set, the default clone URLs become file:///<mirror>/<repo>. The mirror itself is never built in.
include_guard(GLOBAL)

# --- Feature switches -------------------------------------------------------------------------------------------
option(PT_WITH_JOLT   "Build the Jolt 5.6.0 SDK and the targets that use it"   ON)
option(PT_WITH_BULLET "Build the Bullet 3.25 SDK and the targets that use it"  ON)
option(PT_WITH_PHYSX  "Build the PhysX 5.11 SDK and the targets that use it"   ON)
option(PT_WITH_CHAOS  "Chaos bridge through UBT (user-implemented, M2.5 / M13). No-op until then" OFF)
option(PT_WITH_VIEWER "Build viewer-side libraries (Dear ImGui / ImPlot). Viewer targets arrive in M4" ON)
option(PT_DO_CHECK    "Compile check()/verify()/ensure() bodies in harness code (UE DO_CHECK)" ON)
option(PT_PHYSX_USE_BOOTSTRAP "Build PhysX through PhysXBootstrap.cmake (VS generators, or fallback for 03 M0 criterion 4)" OFF)

set(PT_PRESET "custom" CACHE STRING "Configure preset name (recorded in PTBuildInfo.h and BuildFlags.json)")
set(PT_FLAG_PROFILE "Shipped" CACHE STRING "Build flag profile: Shipped (MVP) | MatchedAVX2 (M7, test-avx2: SDKs and adapters /arch:AVX2, harness none)")
set_property(CACHE PT_FLAG_PROFILE PROPERTY STRINGS Shipped MatchedAVX2)

# --- SDK pins (full commit SHAs, verified against D:\PhysicsRefs) ----------------------------------------------
set(PT_JOLT_PIN_SHA   "e77f175595e64cb44218cc9d9d56fc365ad0e36a")   # Jolt v5.6.0
set(PT_PHYSX_PIN_SHA  "da950a3537927784951853c66618036f332ca0ce")   # PhysX 5.11.0 (tag ovphysx-0.6.3)
set(PT_BULLET_PIN_SHA "2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5")   # Bullet 3.25

# --- Clone URLs ----------------------------------------------------------------------------------------------------
set(_pt_mirror_default "")
if(DEFINED ENV{PT_SDK_MIRROR})
	file(TO_CMAKE_PATH "$ENV{PT_SDK_MIRROR}" _pt_mirror_default)
endif()
set(PT_SDK_MIRROR "${_pt_mirror_default}" CACHE PATH
	"Read-only local mirror root with JoltPhysics/, PhysX/, bullet3/ (default: env PT_SDK_MIRROR; empty = GitHub)")
if(PT_SDK_MIRROR)
	file(TO_CMAKE_PATH "${PT_SDK_MIRROR}" PT_SDK_MIRROR)
	string(REGEX REPLACE "/+$" "" PT_SDK_MIRROR "${PT_SDK_MIRROR}")
endif()

function(_pt_default_git_url OutVar RepoDir GitHubUrl)
	if(PT_SDK_MIRROR)
		set(${OutVar} "file:///${PT_SDK_MIRROR}/${RepoDir}" PARENT_SCOPE)
	else()
		set(${OutVar} "${GitHubUrl}" PARENT_SCOPE)
	endif()
endfunction()
_pt_default_git_url(_pt_jolt_url   JoltPhysics "https://github.com/jrouwe/JoltPhysics.git")
_pt_default_git_url(_pt_physx_url  PhysX       "https://github.com/NVIDIA-Omniverse/PhysX.git")
_pt_default_git_url(_pt_bullet_url bullet3     "https://github.com/bulletphysics/bullet3.git")

set(PT_JOLT_GIT   "${_pt_jolt_url}"   CACHE STRING "Jolt git URL (GitHub or file:///<mirror>/JoltPhysics)")
set(PT_PHYSX_GIT  "${_pt_physx_url}"  CACHE STRING "PhysX git URL (GitHub or file:///<mirror>/PhysX)")
set(PT_BULLET_GIT "${_pt_bullet_url}" CACHE STRING "Bullet git URL (GitHub or file:///<mirror>/bullet3)")

# The tags exist so that a wrong value is caught loudly (03 M0 criterion 8); they must equal the pinned SHAs.
set(PT_JOLT_GIT_TAG   "${PT_JOLT_PIN_SHA}"   CACHE STRING "Must equal the pinned Jolt SHA")
set(PT_PHYSX_GIT_TAG  "${PT_PHYSX_PIN_SHA}"  CACHE STRING "Must equal the pinned PhysX SHA")
set(PT_BULLET_GIT_TAG "${PT_BULLET_PIN_SHA}" CACHE STRING "Must equal the pinned Bullet SHA")

# Small header/source dependencies (URL + URL_HASH). PT_DEPS_CACHE optionally points at a directory holding the
# same archives (offline builds); the SHA-256 pin is checked either way.
set(_pt_deps_cache_default "")
if(DEFINED ENV{PT_DEPS_CACHE})
	file(TO_CMAKE_PATH "$ENV{PT_DEPS_CACHE}" _pt_deps_cache_default)
endif()
set(PT_DEPS_CACHE "${_pt_deps_cache_default}" CACHE PATH "Optional directory with pre-downloaded dependency archives")

# Chaos (later): local UE 5.8.3 source tree, used only by PT_WITH_CHAOS.
set(PT_UE_ROOT "" CACHE PATH "UE 5.8.3 source root for the Chaos bridge (PT_WITH_CHAOS only, M2.5 / M13)")

# --- Pin guard (runs before anything is downloaded) ---------------------------------------------------------------
function(_pt_check_pin Name Tag Pin Enabled)
	if(Enabled AND NOT Tag STREQUAL Pin)
		message(FATAL_ERROR
			"PT_${Name}_GIT_TAG='${Tag}' differs from the pinned SHA ${Pin}.\n"
			"SDK revisions are pinned by full commit SHA (docs/02_Architecture.md 4.3, DL54). Changing an SDK "
			"revision is a design change: update the pin in cmake/PTOptions.cmake and record it in docs/04_DecisionLog.md.")
	endif()
endfunction()
_pt_check_pin(JOLT   "${PT_JOLT_GIT_TAG}"   "${PT_JOLT_PIN_SHA}"   ${PT_WITH_JOLT})
_pt_check_pin(PHYSX  "${PT_PHYSX_GIT_TAG}"  "${PT_PHYSX_PIN_SHA}"  ${PT_WITH_PHYSX})
_pt_check_pin(BULLET "${PT_BULLET_GIT_TAG}" "${PT_BULLET_PIN_SHA}" ${PT_WITH_BULLET})

message(STATUS "PhysicsTest: preset=${PT_PRESET} profile=${PT_FLAG_PROFILE} jolt=${PT_WITH_JOLT} bullet=${PT_WITH_BULLET} "
	"physx=${PT_WITH_PHYSX} chaos=${PT_WITH_CHAOS} viewer=${PT_WITH_VIEWER} do_check=${PT_DO_CHECK}")
if(PT_SDK_MIRROR)
	message(STATUS "PhysicsTest: SDK mirror ${PT_SDK_MIRROR} (read-only; clones go to ${CMAKE_BINARY_DIR}/_deps)")
endif()
