
cmake_minimum_required(VERSION 3.20)

project(geforce_now_xbox360_ui C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 23)

set(MBEDTLS_XENON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/mbedtls-xenon" CACHE PATH "Mbed TLS XSDK port")
set(CURL_XENON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/curl-xenon" CACHE PATH "libcurl XSDK port")
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${MBEDTLS_XENON_DIR}" mbedtls)
set(MBEDTLS_INCLUDE_DIR "${MBEDTLS_XENON_DIR}/include" CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory("${CURL_XENON_DIR}" curl)

add_executable(geforce_now_ui
    src/main.cpp
    src/gfx.cpp
)
target_include_directories(geforce_now_ui PRIVATE third_party)
target_link_libraries(geforce_now_ui PRIVATE CURL::libcurl mbedtls)

xsdk_add_shaders(geforce_now_ui shaders/ui.vert shaders/ui.frag)
xsdk_add_xex(geforce_now_ui
    NAME "GeForce NOW UI Preview"
)

configure_file("${MBEDTLS_XENON_DIR}/programs/xsdk/cacert.pem"
               "${CMAKE_CURRENT_BINARY_DIR}/cacert.pem" COPYONLY)
