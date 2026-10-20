# PTModules.cmake - helpers that keep the per-module CMakeLists files short (UE-style Public/Private layout).
include_guard(GLOBAL)

#[[
pt_add_module(<Name> <STATIC|INTERFACE>
	[ALIAS <Namespace::Name>]
	[SOURCES <file>...]
	[PUBLIC_DEPS <target>...] [PRIVATE_DEPS <target>...] [INTERFACE_DEPS <target>...])

	Harness module rooted at the calling directory: Public/ is the public include path, Private/ the private one.
	Applies pt_target_defaults() (C++20, /W4, ...). Target name is <Name>; ALIAS adds e.g. PT::Core.
#]]
function(pt_add_module Name Kind)
	cmake_parse_arguments(PARSE_ARGV 2 Arg "" "ALIAS" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS;INTERFACE_DEPS")
	if(Kind STREQUAL "INTERFACE")
		add_library(${Name} INTERFACE)
		target_include_directories(${Name} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Public")
		if(Arg_INTERFACE_DEPS OR Arg_PUBLIC_DEPS)
			target_link_libraries(${Name} INTERFACE ${Arg_INTERFACE_DEPS} ${Arg_PUBLIC_DEPS})
		endif()
		if(Arg_SOURCES)
			target_sources(${Name} PRIVATE ${Arg_SOURCES})   # IDE visibility only
		endif()
	elseif(Kind STREQUAL "STATIC")
		add_library(${Name} STATIC ${Arg_SOURCES})
		target_include_directories(${Name}
			PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
			PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
		foreach(_scope PUBLIC PRIVATE INTERFACE)
			if(Arg_${_scope}_DEPS)
				target_link_libraries(${Name} ${_scope} ${Arg_${_scope}_DEPS})
			endif()
		endforeach()
	else()
		message(FATAL_ERROR "pt_add_module(${Name}): kind must be STATIC or INTERFACE, got '${Kind}'")
	endif()
	pt_target_defaults(${Name})
	if(Arg_ALIAS)
		add_library(${Arg_ALIAS} ALIAS ${Name})
	endif()
	set_property(TARGET ${Name} PROPERTY FOLDER "PhysicsTest/Runtime")
endfunction()

#[[
pt_add_program(<Name> SOURCES <file>... [DEPS <target>...] [FOLDER <folder>])
	Console executable with the harness compile policy. Output: out/<preset>/bin/<Name>.exe
#]]
function(pt_add_program Name)
	cmake_parse_arguments(PARSE_ARGV 1 Arg "" "FOLDER" "SOURCES;DEPS")
	add_executable(${Name} ${Arg_SOURCES})
	if(Arg_DEPS)
		target_link_libraries(${Name} PRIVATE ${Arg_DEPS})
	endif()
	pt_target_defaults(${Name})
	if(NOT Arg_FOLDER)
		set(Arg_FOLDER "PhysicsTest/Programs")
	endif()
	set_property(TARGET ${Name} PROPERTY FOLDER "${Arg_FOLDER}")
endfunction()

#[[
pt_add_script_test(<TestName> SCRIPT <file.cmake> [ARGS <-Dvar=value>...] [LABELS <label>...])
	CTest entry that runs a CMake -P probe script with the given -D arguments.
#]]
function(pt_add_script_test TestName)
	cmake_parse_arguments(PARSE_ARGV 1 Arg "" "SCRIPT" "ARGS;LABELS")
	add_test(NAME ${TestName} COMMAND "${CMAKE_COMMAND}" ${Arg_ARGS} -P "${Arg_SCRIPT}")
	if(Arg_LABELS)
		set_tests_properties(${TestName} PROPERTIES LABELS "${Arg_LABELS}")
	endif()
endfunction()
