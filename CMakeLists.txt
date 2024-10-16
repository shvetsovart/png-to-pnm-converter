cmake_minimum_required(VERSION 3.20)
project(labs)

set(CMAKE_CXX_STANDARD 14)

add_executable(labs c2.c)
add_subdirectory(libdeflate-master)
add_subdirectory(isa-l-master)
add_subdirectory(zlib-1.2.12)