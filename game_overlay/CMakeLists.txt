project(game_overlay)

find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG QUIET)
find_package(WebP CONFIG QUIET)
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)

if (NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
            URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif ()

if (NOT WebP_FOUND)
    include(FetchContent)
    set(_ra3_saved_shared_libs "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF)
    set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(libwebp
            URL https://github.com/webmproject/libwebp/archive/refs/tags/v1.5.0.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(libwebp)
    set(BUILD_SHARED_LIBS "${_ra3_saved_shared_libs}")
    if (NOT TARGET WebP::webp)
        add_library(WebP::webp ALIAS webp)
    endif ()
endif ()

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
        src/ra3.ixx
        src/ra3-address.ixx
        src/ra3-types.ixx
        src/hub.ixx
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${DETOURS_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME} PRIVATE
        imgui::imgui
        nlohmann_json::nlohmann_json
        WebP::webp
        d3d9
        winhttp
        ole32
        windowscodecs
        ${DETOURS_LIBRARY}
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
