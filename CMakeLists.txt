# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

cmake_minimum_required(VERSION 3.15)

file(READ "package.xml" PACKAGE_XML_CONTENT)
string(REGEX MATCH "<version>([0-9\\.]+)</version>" _ "${PACKAGE_XML_CONTENT}")
project(roboparty_dexhand VERSION ${CMAKE_MATCH_1} LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(CTest)
include(GNUInstallDirs)
include(cmake/SelectLHandProSdk.cmake)

find_package(ament_cmake QUIET)
find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 REQUIRED)

roboparty_dexhand_normalize_arch("${CMAKE_SYSTEM_PROCESSOR}"
  DEXHAND_SDK_ARCH)
set(DEXHAND_SDK_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/${DEXHAND_SDK_ARCH}")
set(DEXHAND_SDK_LIBRARY "${DEXHAND_SDK_DIR}/libLHandProLib.so")
if(NOT EXISTS "${DEXHAND_SDK_LIBRARY}")
  message(FATAL_ERROR
          "Missing ${DEXHAND_SDK_ARCH} LHandPro SDK: ${DEXHAND_SDK_LIBRARY}")
endif()

add_library(roboparty_dexhand::lhandpro_sdk SHARED IMPORTED GLOBAL)
set_target_properties(
  roboparty_dexhand::lhandpro_sdk
  PROPERTIES IMPORTED_LOCATION "${DEXHAND_SDK_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES
  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/include")

add_subdirectory(src)

pybind11_add_module(dexhand_py src/pybind_module.cpp)
target_compile_features(dexhand_py PRIVATE cxx_std_17)
target_link_libraries(dexhand_py PRIVATE dexhand)
set_target_properties(dexhand_py PROPERTIES INSTALL_RPATH "\$ORIGIN/../..")

set(PYTHON_INSTALL_DIR
    "${CMAKE_INSTALL_LIBDIR}/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages")

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${DEXHAND_SDK_LIBRARY}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(
  TARGETS dexhand
  EXPORT roboparty_dexhand-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS dexhand_py
        LIBRARY DESTINATION ${PYTHON_INSTALL_DIR}
        RUNTIME DESTINATION ${PYTHON_INSTALL_DIR})
install(
  EXPORT roboparty_dexhand-targets
  FILE roboparty_dexhandTargets.cmake
  NAMESPACE roboparty_dexhand::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/roboparty_dexhand)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhandConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMinorVersion)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/roboparty_dexhandConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhandConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/roboparty_dexhand)
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhandConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhandConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/roboparty_dexhand)

option(DEXHAND_ENABLE_VCAN_TESTS "Enable privileged vcan integration test" OFF)
if(BUILD_TESTING)
  add_subdirectory(tests)
  if(ament_cmake_FOUND)
    find_package(ament_lint_auto REQUIRED)
    # The repository gate checks the required GPL-3.0 SPDX form exactly.
    # Keep the established C++ style and the immutable vendor header untouched.
    set(AMENT_LINT_AUTO_EXCLUDE
      ament_cmake_copyright
      ament_cmake_cpplint
      ament_cmake_uncrustify)
    set(AMENT_LINT_AUTO_FILE_EXCLUDE
      thirdparty/include/LHandProLib/LHandProLib.h)
    ament_lint_auto_find_test_dependencies()
  endif()
endif()

if(ament_cmake_FOUND)
  ament_export_targets(roboparty_dexhand-targets HAS_LIBRARY_TARGET)
  ament_export_dependencies(fmt spdlog)
  ament_export_include_directories(include)
  ament_package()
endif()
