# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

add_library(lhandpro_driver STATIC lhandpro_driver.cpp lhandpro_sdk.cpp
                                   lhandpro_feedback_period.cpp)
target_compile_features(lhandpro_driver PUBLIC cxx_std_17)
set_target_properties(lhandpro_driver PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(
  lhandpro_driver
  PUBLIC ${PROJECT_SOURCE_DIR}/src
  PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(
  lhandpro_driver
  PRIVATE dexhand_canfd roboparty_dexhand::lhandpro_sdk fmt::fmt
          spdlog::spdlog Threads::Threads)
