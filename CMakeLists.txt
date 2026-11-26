
cmake_minimum_required(VERSION 3.20)

project(geforce_now_xbox360_ui C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 23)

add_executable(geforce_now_ui
    src/main.cpp
    src/gfx.cpp
)

xsdk_add_shaders(geforce_now_ui shaders/ui.vert shaders/ui.frag)
xsdk_add_xex(geforce_now_ui
    NAME "GeForce NOW UI Preview"
)
