# PtFetch.cmake - pinned FetchContent helpers shared by the ThirdParty modules (D3, DL54, 03 M0 task 4).
#
# Every dependency is downloaded into the build tree (${FETCHCONTENT_BASE_DIR} = out/<preset>/_deps) and only
# POPULATED here: SOURCE_SUBDIR points at a directory without CMakeLists.txt, so FetchContent_MakeAvailable() does
# not add anything (CMake Modules/FetchContent.cmake: add_subdirectory only `if(EXISTS <srcdir>/CMakeLists.txt)`).
# The calling module then adds the SDK itself with its own option block, EXCLUDE_FROM_ALL and SYSTEM.
include_guard(GLOBAL)
include(FetchContent)
find_package(Git REQUIRED)

set(PT_FETCH_POPULATE_ONLY "_pt_populate_only_")   # intentionally missing directory

# Comparison key for a path on Windows: absolute, symlinks resolved, normalised, lower-case (NTFS paths are
# case-insensitive; cmake_path(IS_PREFIX) is purely lexical and case-sensitive).
function(_pt_path_key Path OutVar)
	file(REAL_PATH "${Path}" _real)
	cmake_path(NORMAL_PATH _real)
	string(TOLOWER "${_real}" _real)
	set(${OutVar} "${_real}" PARENT_SCOPE)
endfunction()

#[[
pt_fetch_git_pinned(<Name> URL <git url> TAG <sha> PIN <sha>)
	Clones <url> at the pinned commit (GIT_TAG is the full SHA: no tag names, no GIT_SHALLOW), then
	  - refuses a source directory that is not inside this build tree's FETCHCONTENT_BASE_DIR (out/<preset>/_deps):
	    SDK configure steps write into their source tree (PhysX: physx/include/PxConfig.h), so the read-only mirror,
	    the UE tree or any other external checkout must never be the source directory, whether PT_SDK_MIRROR is set
	    or not. In practice this rejects every FETCHCONTENT_SOURCE_DIR_<NAME> override that points outside the build tree.
	  - verifies `git rev-parse HEAD` == PIN.
	Sets PT_<NAME>_SOURCE_DIR / PT_<NAME>_BINARY_DIR (upper-case NAME) in the caller.
#]]
function(pt_fetch_git_pinned Name)
	cmake_parse_arguments(PARSE_ARGV 1 Arg "" "URL;TAG;PIN" "")
	if(NOT Arg_TAG STREQUAL Arg_PIN)
		message(FATAL_ERROR "${Name}: GIT_TAG '${Arg_TAG}' differs from the pinned SHA '${Arg_PIN}'")
	endif()

	FetchContent_Declare(${Name}
		GIT_REPOSITORY "${Arg_URL}"
		GIT_TAG        "${Arg_PIN}"
		GIT_SUBMODULES ""
		GIT_CONFIG     core.longpaths=true advice.detachedHead=false
		SOURCE_SUBDIR  "${PT_FETCH_POPULATE_ONLY}")
	FetchContent_MakeAvailable(${Name})
	FetchContent_GetProperties(${Name} SOURCE_DIR _src BINARY_DIR _bin)
	string(TOUPPER "${Name}" _upper)

	# Location guard (case-insensitive). The mirror test only gives the more specific message; the build-tree test
	# below is the rule.
	_pt_path_key("${_src}" _src_key)
	if(PT_SDK_MIRROR)
		_pt_path_key("${PT_SDK_MIRROR}" _mirror_key)
		cmake_path(IS_PREFIX _mirror_key "${_src_key}" _inside_mirror)
		if(_inside_mirror)
			message(FATAL_ERROR
				"${Name}: source directory '${_src}' is inside the read-only mirror '${PT_SDK_MIRROR}'. "
				"SDKs are never built in place (PhysX writes PxConfig.h into its source tree). "
				"Unset FETCHCONTENT_SOURCE_DIR_${_upper} and let FetchContent clone into the build tree.")
		endif()
	endif()
	_pt_path_key("${FETCHCONTENT_BASE_DIR}" _base_key)
	cmake_path(IS_PREFIX _base_key "${_src_key}" _inside_build_tree)
	if(NOT _inside_build_tree)
		message(FATAL_ERROR
			"${Name}: source directory '${_src}' is outside this build tree's FETCHCONTENT_BASE_DIR "
			"'${FETCHCONTENT_BASE_DIR}'. SDKs are never built in an external checkout (their configure step writes "
			"into the source tree). Unset FETCHCONTENT_SOURCE_DIR_${_upper}; to modify an SDK locally, edit the clone "
			"under ${FETCHCONTENT_BASE_DIR} (FETCHCONTENT_UPDATES_DISCONNECTED keeps it).")
	endif()

	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${_src}" rev-parse HEAD
		OUTPUT_VARIABLE _head
		ERROR_VARIABLE _err
		RESULT_VARIABLE _rc
		OUTPUT_STRIP_TRAILING_WHITESPACE)
	if(NOT _rc EQUAL 0 OR NOT _head STREQUAL Arg_PIN)
		message(FATAL_ERROR
			"${Name}: '${_src}' is at HEAD '${_head}' (git rc ${_rc} ${_err}), expected the pinned SHA ${Arg_PIN}.")
	endif()
	message(STATUS "${Name}: ${Arg_URL} @ ${_head} (verified)")

	set(PT_${_upper}_SOURCE_DIR "${_src}" PARENT_SCOPE)
	set(PT_${_upper}_BINARY_DIR "${_bin}" PARENT_SCOPE)
	set_property(GLOBAL APPEND PROPERTY PT_SDK_SOURCE_DIRS "${Name}=${_src}")
endfunction()

#[[
pt_fetch_url_pinned(<Name> URL <url> SHA256 <hash> [CACHE_FILE <archive file name>])
	Downloads and extracts an archive whose SHA-256 is pinned. When PT_DEPS_CACHE contains CACHE_FILE, that local
	copy is used instead of the URL (offline builds); the hash is verified either way.
	Sets PT_<NAME>_SOURCE_DIR (upper-case NAME) in the caller.
#]]
function(pt_fetch_url_pinned Name)
	cmake_parse_arguments(PARSE_ARGV 1 Arg "" "URL;SHA256;CACHE_FILE" "")
	set(_url "${Arg_URL}")
	if(PT_DEPS_CACHE AND Arg_CACHE_FILE AND EXISTS "${PT_DEPS_CACHE}/${Arg_CACHE_FILE}")
		set(_url "${PT_DEPS_CACHE}/${Arg_CACHE_FILE}")
	endif()
	FetchContent_Declare(${Name}
		URL      "${_url}"
		URL_HASH SHA256=${Arg_SHA256}
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
		DOWNLOAD_NO_PROGRESS TRUE
		SOURCE_SUBDIR "${PT_FETCH_POPULATE_ONLY}")
	FetchContent_MakeAvailable(${Name})
	FetchContent_GetProperties(${Name} SOURCE_DIR _src)
	string(TOUPPER "${Name}" _upper)
	set(PT_${_upper}_SOURCE_DIR "${_src}" PARENT_SCOPE)
endfunction()
