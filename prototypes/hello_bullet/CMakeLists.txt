cmake_minimum_required(VERSION 3.25)   # 3.25: add_subdirectory(... SYSTEM)
project(PhysicsTestBulletHello LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# One CRT for every engine (Bullet/PhysX/Jolt/Chaos-later): /MD, /MDd
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PtBullet)

add_executable(hello_bullet src/hello_bullet.cpp)
target_link_libraries(hello_bullet PRIVATE PT::Bullet)
target_compile_options(hello_bullet PRIVATE /W4 /permissive- /utf-8)
