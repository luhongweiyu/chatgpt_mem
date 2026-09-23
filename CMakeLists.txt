cmake_minimum_required(VERSION 3.10)
project(mem LANGUAGES C CXX)

add_library(mem SHARED
    src/mem.cpp
)

target_include_directories(mem PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(mem PRIVATE cxx_std_17)
target_compile_options(mem PRIVATE -Wall -Wextra -Wpedantic -fvisibility=hidden)

set(MEM_ALLOWED_EXE_PATH "" CACHE STRING "Exact /proc/<pid>/exe path accepted by init()")
if(NOT MEM_ALLOWED_EXE_PATH STREQUAL "")
    target_compile_definitions(mem PRIVATE MEM_ALLOWED_EXE_PATH="${MEM_ALLOWED_EXE_PATH}")
endif()

if(ANDROID)
    find_library(log-lib log)
    target_link_libraries(mem PRIVATE ${log-lib})
endif()

set_target_properties(mem PROPERTIES
    OUTPUT_NAME "mem"
)
