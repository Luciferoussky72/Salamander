cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_COMPILER g++-16)
project(Salamander)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ZLIB REQUIRED)
find_package(SDL3 REQUIRED)

add_executable(salamander main.cpp)

target_compile_options(salamander PRIVATE
        # Set the STL to the C++26 STL
        -std=c++26
        # Turn on reflection (has no runtime cost, thankfully, but we still need to tell the compiler to turn it on)
        -freflection
        # Turn off exceptions
        -fno-exceptions
        # Turn off errno to speed up math functions
        -fno-math-errno
        # The warning flags
        -Wall -Wextra -Wpedantic -Werror
        # Conditionally compile based on whether it's a debug or release build
        $<$<CONFIG:Debug>: -O0 -g>
        $<$<CONFIG:Release>: -O2
        # Turn off runtime type information for a smaller binary
        -fno-rtti
        >
)

target_link_options(salamander PRIVATE
        $<$<CONFIG:Release>:
        # Strip symbols from the release executable to optimize its size
        -s
        # Turn on link-time optimization
        -flto
        >
)

target_include_directories(salamander PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../Loam/include
)
target_link_directories(salamander PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../Loam/cmake-build-release
)
target_include_directories(salamander PRIVATE "../Loam/include/")
target_include_directories(salamander PRIVATE "../Reflection/")
target_link_libraries(salamander PRIVATE
        ZLIB::ZLIB
        SDL3::SDL3
        loam
)
