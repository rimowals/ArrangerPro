cmake_minimum_required(VERSION 3.25)
project(ArrangerPro VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warning/conformance flags for our own targets only (not for fetched dependencies).
add_library(arranger_options INTERFACE)
if(MSVC)
    target_compile_options(arranger_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()

option(ARRANGER_BUILD_APP "Build the JUCE desktop application" ON)

add_subdirectory(core)
add_subdirectory(tools/arrtool)

if(ARRANGER_BUILD_APP)
    include(FetchContent)
    FetchContent_Declare(juce
        URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.9.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(juce)
    add_subdirectory(app)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
