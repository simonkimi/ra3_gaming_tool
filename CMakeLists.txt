cmake_minimum_required(VERSION 3.28)

if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif ()
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
if (NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()
set(BUILD_SHARED_LIBS OFF)

project(ra3_tool)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif ()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake;${CMAKE_MODULE_PATH}")
set(VCPKG_FEATURE_FLAGS "version")
include(MsvcIfcSearchDir)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/dxifcInlineFunctions->")

add_definitions("-DUNICODE" "-D_UNICODE")

add_subdirectory(win32_utils)
add_subdirectory(game_overlay)
add_subdirectory(game_injector)

msvc_ifc_search_dirs()