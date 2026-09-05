project(ra3_gaming_tool)

find_package(imgui CONFIG REQUIRED)
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)

add_library(${PROJECT_NAME} SHARED src/main.cpp exports.def)

target_sources(${PROJECT_NAME}
        PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS src
        FILES
        src/overlay.ixx
        src/overlay-utils.ixx
        src/overlay-ui.ixx
        src/overlay-input.ixx
        src/overlay-dx9.ixx
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${DETOURS_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME} PRIVATE
        imgui::imgui
        d3d9
        ${DETOURS_LIBRARY}
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
