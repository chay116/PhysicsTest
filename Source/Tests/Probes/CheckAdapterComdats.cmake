# CheckAdapterComdats.cmake - ODR / flag isolation at binary level (docs/Abi.md 5.13, 02_Architecture 7.3, DL46; M3 fix
# round 0). An SDK adapter library (PtBackendJolt: /arch:AVX2) must not be the object the linker took a shared COMDAT
# from: every function the map attributes to an adapter object has to be the backend's own (a symbol that names a type or
# namespace only that backend has), a lambda, the static entry point, or a compiler-generated sized-delete helper. A
# std::vector<uint32>::push_back from an adapter object fails, because harness (Shipped, no /arch) code calling it would
# run the AVX2 copy. Functions the SDK library itself supplies outside its namespace are listed as well: those come from
# the SDK's own sources (no local patch, Q8), so the known ones are allowed by exact name and any new one fails.
#   cmake -DPT_MAPS=<a.map;b.map> -DPT_ADAPTER_LIBS=PtBackendJolt -DPT_ADAPTER_OWNED=<regex>
#         -DPT_SDK_LIBS=Jolt -DPT_SDK_OWNED=<regex> -DPT_STRICT=ON|OFF -P CheckAdapterComdats.cmake
# PT_STRICT=OFF (the Debug preset) only reports: /Od emits every header-inline helper of the SDK's own headers out of line
# (std::min<float>, ::abs(float), fabsf, std::atomic<uint32>), which cannot be isolated without patching the SDK. The dbg
# exe is never a measurement build (docs/Progress.md M3 §6).
cmake_minimum_required(VERSION 3.25)
foreach(_req PT_MAPS PT_ADAPTER_LIBS PT_ADAPTER_OWNED)
	if(NOT DEFINED ${_req})
		message(FATAL_ERROR "${_req} is required")
	endif()
endforeach()
# Compiler-generated helpers of deleting destructors (/Zc:sizedDealloc): forward to operator delete, no arithmetic.
set(_compiler_helpers "^\\?__(empty_)?global(_array)?_delete@@")
# Known SDK residue of the optimised presets, per SDK library: functions the SDK's own TUs (not the adapter) define out of
# line outside the SDK namespace. None does floating-point arithmetic except ::abs(float), which only clears the sign bit
# (exact on every ISA), so whichever copy the linker keeps cannot change a harness result. The list is exact: a new
# shared function (an SDK update, a new adapter include) fails the probe until it is reviewed and listed here.
set(_known_Jolt
	"??$sleep_for@_JU?$ratio@$00$0PECEA@@std@@@this_thread@std@@YAXAEBV?$duration@_JU?$ratio@$00$0PECEA@@std@@@chrono@1@@Z"   # JobSystemThreadPool.cpp
	"?now@steady_clock@chrono@std@@SA?AV?$time_point@Usteady_clock@chrono@std@@V?$duration@_JU?$ratio@$00$0DLJKMKAA@@std@@@23@@23@XZ"   # JobSystemThreadPool.cpp
	"?terminate@std@@YAXXZ"   # JobSystemThreadPool.cpp (std::thread's destructor)
	"?abs@@YAMM@Z")           # ConvexShape.cpp: ::abs(float) of <cmath>

set(_libs ${PT_ADAPTER_LIBS} ${PT_SDK_LIBS})
list(JOIN _libs "|" _lib_alt)
set(_failures "")
set(_reports "")
foreach(_map IN LISTS PT_MAPS)
	if(NOT EXISTS "${_map}")
		message(FATAL_ERROR "linker map '${_map}' not found")
	endif()
	get_filename_component(_map_name "${_map}" NAME)
	# Only the "Publics by Value" part: the "Static symbols" part lists file-local functions, which never merge.
	file(READ "${_map}" _text)
	string(FIND "${_text}" "Static symbols" _static_at)
	if(_static_at GREATER 0)
		string(SUBSTRING "${_text}" 0 ${_static_at} _text)
	endif()
	string(REGEX MATCHALL "\n [0-9a-fA-F]+:[0-9a-fA-F]+ +[^ \n]+ +[0-9a-fA-F]+ f [ i] (${_lib_alt}):[^ \n]+" _lines "${_text}")
	set(_count_adapter 0)
	set(_count_sdk 0)
	foreach(_line IN LISTS _lines)
		string(REGEX MATCH "^\n [0-9a-fA-F]+:[0-9a-fA-F]+ +([^ \n]+) +[0-9a-fA-F]+ f [ i] (${_lib_alt}):([^ \n]+)$" _m "${_line}")
		set(_symbol "${CMAKE_MATCH_1}")
		set(_lib "${CMAKE_MATCH_2}")
		set(_obj "${CMAKE_MATCH_3}")
		if(_lib IN_LIST PT_ADAPTER_LIBS)
			math(EXPR _count_adapter "${_count_adapter} + 1")
			if(_symbol MATCHES "${PT_ADAPTER_OWNED}" OR _symbol MATCHES "<lambda_" OR _symbol MATCHES "^PT_GetBackendApi_" OR _symbol MATCHES "${_compiler_helpers}")
				continue()
			endif()
			list(APPEND _failures "${_map_name}: ${_symbol} (${_lib}:${_obj})")
		else()
			math(EXPR _count_sdk "${_count_sdk} + 1")
			if((DEFINED PT_SDK_OWNED AND _symbol MATCHES "${PT_SDK_OWNED}") OR _symbol MATCHES "<lambda_" OR _symbol MATCHES "${_compiler_helpers}")
				continue()
			endif()
			if(_symbol IN_LIST _known_${_lib})
				list(APPEND _reports "${_map_name}: known SDK residue ${_symbol} (${_lib}:${_obj})")
				continue()
			endif()
			list(APPEND _failures "${_map_name}: ${_symbol} (${_lib}:${_obj}, SDK source)")
		endif()
	endforeach()
	message(STATUS "${_map_name}: ${_count_adapter} adapter functions, ${_count_sdk} SDK functions checked")
	if(_count_adapter EQUAL 0)
		message(FATAL_ERROR "${_map_name}: no function from ${PT_ADAPTER_LIBS} in the map (wrong map or library name?)")
	endif()
endforeach()
foreach(_report IN LISTS _reports)
	message(STATUS "${_report}")
endforeach()
if(_failures)
	list(LENGTH _failures _n)
	list(JOIN _failures "\n  " _text)
	if(PT_STRICT)
		message(FATAL_ERROR "${_n} shared COMDAT(s) supplied by an /arch-isolated object (docs/Abi.md 5.13):\n  ${_text}")
	endif()
	message(STATUS "REPORT ONLY (PT_STRICT=OFF): ${_n} shared COMDAT(s) supplied by an /arch-isolated object:\n  ${_text}")
	message(STATUS "PASS (report only): adapter COMDAT isolation is enforced in optimised presets")
	return()
endif()
message(STATUS "PASS: every function the adapters (${PT_ADAPTER_LIBS}) supply is backend-owned; SDK residue is known")
