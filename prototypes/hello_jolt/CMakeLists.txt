cmake_minimum_required(VERSION 3.25...3.31)   # block() needs 3.25; policy max 3.31 -> CMP0168 NEW (no FetchContent sub-build)
project(PhysicsTestJoltHello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# One CRT for the whole project, set BEFORE any third-party add_subdirectory (PhysX vc17win64 preset = /MD, UE = /MD).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Jolt.cmake)

add_executable(JoltHello main.cpp)
target_link_libraries(JoltHello PRIVATE Jolt::Jolt)
if (MSVC)
	target_compile_options(JoltHello PRIVATE /W4 /permissive-)
endif()

# --- Seam test: engine-agnostic header, Jolt linked PRIVATE so /arch:AVX2 and JPH_* stay inside the backend target
add_library(PTBackendJolt STATIC seam/BackendJolt.cpp seam/BackendJolt.h)
target_include_directories(PTBackendJolt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/seam)
target_link_libraries(PTBackendJolt PRIVATE Jolt::Jolt)
add_executable(SeamTest seam/SeamMain.cpp)
target_link_libraries(SeamTest PRIVATE PTBackendJolt)
