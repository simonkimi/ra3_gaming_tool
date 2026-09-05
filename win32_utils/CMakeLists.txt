project(win32_utils)

add_library(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
        PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS src
        FILES
        src/win32.ixx
        src/win32-raii.ixx
        src/win32-process.ixx
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
msvc_intellisense_ifc_dir(${PROJECT_NAME})
