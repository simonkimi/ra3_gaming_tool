project(ra3_gaming_tool)

find_package(imgui CONFIG REQUIRED)
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)
include_directories(include)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "include/*.h" "src/*.cpp" "src/*.h")

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
        ${DETOURS_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME} PRIVATE
        imgui::imgui
        d3d9
        ${DETOURS_LIBRARY}
)
target_precompile_headers(${PROJECT_NAME} PRIVATE include/pch.h)