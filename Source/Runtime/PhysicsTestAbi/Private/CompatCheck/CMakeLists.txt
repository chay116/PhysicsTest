# Compile checks of the backend boundary (03 M2 criterion 1, A.3). This directory holds nothing else: the /EHsc removal
# below is directory-scoped and must stay confined to the UE-like check.
set(_PtAbiPublic "${CMAKE_CURRENT_SOURCE_DIR}/../../Public")

# The ABI header compiles warning-free as C11 under /W4 /WX ...
add_library(PtAbiC11Check OBJECT PtAbiC11Check.c)
target_include_directories(PtAbiC11Check PRIVATE "${_PtAbiPublic}")
set_target_properties(PtAbiC11Check PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "PhysicsTest/Runtime")
# /we4668 (an undefined macro in #if is an error) as UBT adds it for BuildSettingsVersion V6 / Latest (M2 review, M3).
target_compile_options(PtAbiC11Check PRIVATE /W4 /WX /utf-8 /we4668)

# ... and the Kit (with a trivial backend instantiated) compiles warning-free with UE-like flags: /GR- /EHs-c-
# _HAS_EXCEPTIONS=0 PT_ABI_NO_EXCEPTIONS /W4 /WX /we4668 (A.3; /we4668 is UBT's default for the Chaos bridge, M3).
# CMAKE_CXX_FLAGS carries CMake's default /EHsc; a later /EHs-c- would only override it with command-line warning D9025,
# so this directory compiles without it (CMAKE_CXX_FLAGS is directory-scoped: the only C++ target of this directory is
# the compat check).
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
add_library(PtAbiUeCompatCheck OBJECT PtAbiUeCompatCheck.cpp)
target_include_directories(PtAbiUeCompatCheck PRIVATE "${_PtAbiPublic}")
target_compile_features(PtAbiUeCompatCheck PRIVATE cxx_std_20)
target_compile_options(PtAbiUeCompatCheck PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /GR- /EHs-c- /we4668)
target_compile_definitions(PtAbiUeCompatCheck PRIVATE _HAS_EXCEPTIONS=0 PT_ABI_NO_EXCEPTIONS PT_BACKEND_NS=UeCompatCheck)
set_property(TARGET PtAbiUeCompatCheck PROPERTY FOLDER "PhysicsTest/Runtime")
