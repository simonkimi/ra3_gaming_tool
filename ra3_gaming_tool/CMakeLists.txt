project(ra3_gaming_tool)

find_package(spdlog CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)
include_directories(include)

file(GLOB_RECURSE IMGUI_BACKENDS "lib/imgui_backends/*.cpp" "lib/imgui_backends/*.h")
file(GLOB_RECURSE SOURCES "include/*.h" "src/*.cpp" "src/*.h")


add_library(${PROJECT_NAME} SHARED ${IMGUI_BACKENDS} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
        ${DETOURS_INCLUDE_DIRS}
        "lib/imgui_backends"
)
target_link_libraries(${PROJECT_NAME} PRIVATE
        fmt::fmt-header-only
        spdlog::spdlog_header_only
        imgui::imgui
        ${DETOURS_LIBRARY}
)
target_precompile_headers(${PROJECT_NAME} PRIVATE include/pch.h)