
cmake_minimum_required(VERSION 3.20)

project(xbox360_cloud_client C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_executable(xbox360_cloud
    src/main.cpp
    src/gfx.cpp
)
