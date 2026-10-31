cmake_minimum_required(VERSION 3.25)
project(ArrangerPro VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()

add_subdirectory(core)
add_subdirectory(tools/arrtool)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
