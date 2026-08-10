# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty
cmake_minimum_required(VERSION 3.12)

file(READ "package.xml" PACKAGE_XML_CONTENT)
string(REGEX MATCH "<version>([0-9\\.]+)</version>" _ ${PACKAGE_XML_CONTENT})
project(roboparty_dexhand VERSION ${CMAKE_MATCH_1})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
if(NOT CMAKE_CROSSCOMPILING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_BUILD_TYPE Release)

# ===== Dependencies =====
find_package(ament_cmake QUIET)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 REQUIRED)

# roboparty_motors provides MotorsCANFD (motors_canfd library + canfd_iso.hpp)
# Three modes:
#   1. find_package(roboparty_motors) — installed system package
#   2. MOTORS_PREBUILT_DIR — use pre-built .a libs (e.g. from test/motors/build)
#   3. ROBOPARTY_MOTORS_SOURCE_DIR — build from source (add_subdirectory)
find_package(roboparty_motors QUIET)
if(roboparty_motors_FOUND)
    set(MOTORS_CANFD_TARGET roboparty_motors::motors_canfd)
    set(MOTORS_INCLUDE_DIRS "")
elseif(DEFINED MOTORS_PREBUILT_DIR)
    # Use pre-built static libraries + headers from another build tree
    message(STATUS "Using pre-built roboparty_motors from: ${MOTORS_PREBUILT_DIR}")
    add_library(motors_canfd STATIC IMPORTED)
    set_target_properties(motors_canfd PROPERTIES
        IMPORTED_LOCATION "${MOTORS_PREBUILT_DIR}/libmotors_canfd.a")
    set(MOTORS_CANFD_TARGET motors_canfd)
    set(MOTORS_INCLUDE_DIRS
        "${MOTORS_PREBUILT_INCLUDE_DIR}/include"
        "${MOTORS_PREBUILT_INCLUDE_DIR}/src")
elseif(DEFINED ROBOPARTY_MOTORS_SOURCE_DIR)
    message(STATUS "roboparty_motors not installed, using source: ${ROBOPARTY_MOTORS_SOURCE_DIR}")
    add_subdirectory(${ROBOPARTY_MOTORS_SOURCE_DIR} ${CMAKE_BINARY_DIR}/roboparty_motors)
    set(MOTORS_CANFD_TARGET motors_canfd)
    set(MOTORS_INCLUDE_DIRS
        ${ROBOPARTY_MOTORS_SOURCE_DIR}/include
        ${ROBOPARTY_MOTORS_SOURCE_DIR}/src)
else()
    message(FATAL_ERROR
        "roboparty_motors not found. Either install it, or set one of:\n"
        "  -DMOTORS_PREBUILT_DIR=/path/to/motors/build/src/protocol/canfd\n"
        "  -DMOTORS_PREBUILT_INCLUDE_DIR=/path/to/roboparty_motors\n"
        "  -DROBOPARTY_MOTORS_SOURCE_DIR=/path/to/roboparty_motors")
endif()

set(PUBLIC_DEPENDENCIES fmt::fmt spdlog::spdlog pthread)

# ===== Vendor driver: LHandPro =====
add_subdirectory(src/drivers/lhandpro)

# ===== Aggregate library: dexhand =====
add_library(dexhand STATIC
    src/hand_driver.cpp
)

target_include_directories(dexhand
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(dexhand PUBLIC
    ${PUBLIC_DEPENDENCIES}
    lhandpro_driver
    ${MOTORS_CANFD_TARGET}
)

# Add motors include paths if using source tree
if(MOTORS_INCLUDE_DIRS)
    target_include_directories(dexhand PRIVATE ${MOTORS_INCLUDE_DIRS})
endif()

# ===== Install headers =====
install(DIRECTORY include/ DESTINATION include)

# ===== Install the closed-source SDK shared lib =====
install(FILES thirdparty/lib/libLHandProLib.so
    DESTINATION lib
)

# ===== Export targets =====
install(TARGETS dexhand lhandpro_driver
    EXPORT roboparty_dexhand-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(EXPORT roboparty_dexhand-targets
    FILE roboparty_dexhandTargets.cmake
    NAMESPACE roboparty_dexhand::
    DESTINATION lib/cmake/roboparty_dexhand
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhandConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/roboparty_dexhandConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhandConfig.cmake"
    INSTALL_DESTINATION lib/cmake/roboparty_dexhand
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhandConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhandConfigVersion.cmake"
    DESTINATION lib/cmake/roboparty_dexhand
)

# ===== Python module: dexhand_py =====
pybind11_add_module(dexhand_py
    src/pybind_module.cpp
)

target_include_directories(dexhand_py
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(dexhand_py PUBLIC dexhand)

set(PYTHON_INSTALL_DIR "lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages")

install(TARGETS dexhand_py
    LIBRARY DESTINATION ${PYTHON_INSTALL_DIR}
    ARCHIVE DESTINATION ${PYTHON_INSTALL_DIR}
    RUNTIME DESTINATION ${PYTHON_INSTALL_DIR}
)

if(ament_cmake_FOUND)
    ament_export_libraries(dexhand lhandpro_driver)
    ament_export_include_directories(include)
    ament_package()
endif()
