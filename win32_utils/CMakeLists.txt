project(win32_utils)

include_directories(include)
file(GLOB_RECURSE SOURCES_FILE "include/*.h" "src/*.cpp")

add_library(${PROJECT_NAME} ${SOURCES_FILE})

target_precompile_headers("${PROJECT_NAME}" PRIVATE "include/pch.h")