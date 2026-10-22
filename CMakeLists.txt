cmake_minimum_required(VERSION 3.20)
project(NFSMWCashForDamage VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "speed.exe is 32-bit: configure an x86 toolchain (vcvarsall.bat x86).")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "MSVC is required (the plugin uses SEH __try/__except).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: the .asi must not depend on the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(CFD_BUILD_TESTS "Build unit tests" ON)

add_compile_options(/utf-8 /W4 /permissive-)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

# Logic without WinAPI, shared by the plugin and the tests.
add_library(cfd_core STATIC
    src/Config.cpp
    src/Tracker.cpp
)
target_include_directories(cfd_core PUBLIC src)

add_library(NFSMWCashForDamage SHARED
    src/Game.cpp
    src/Log.cpp
    src/Main.cpp
    src/Memory.cpp
)
target_link_libraries(NFSMWCashForDamage PRIVATE cfd_core)
target_compile_definitions(NFSMWCashForDamage PRIVATE CFD_VERSION="${PROJECT_VERSION}")
set_target_properties(NFSMWCashForDamage PROPERTIES PREFIX "" SUFFIX ".asi")

if(CFD_BUILD_TESTS)
    enable_testing()
    foreach(test TrackerTest ConfigTest)
        add_executable(${test} tests/${test}.cpp)
        target_link_libraries(${test} PRIVATE cfd_core)
        target_compile_definitions(${test} PRIVATE CFD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        add_test(NAME ${test} COMMAND ${test})
    endforeach()
endif()
