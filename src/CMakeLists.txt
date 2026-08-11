# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

add_subdirectory(protocol)
add_subdirectory(drivers)

add_library(dexhand SHARED hand_driver.cpp)
target_compile_features(dexhand PUBLIC cxx_std_17)
target_include_directories(
  dexhand
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  dexhand
  PUBLIC fmt::fmt spdlog::spdlog
  PRIVATE lhandpro_driver Threads::Threads)
set_target_properties(
  dexhand PROPERTIES BUILD_RPATH "${DEXHAND_SDK_DIR}"
  INSTALL_RPATH "\$ORIGIN")
