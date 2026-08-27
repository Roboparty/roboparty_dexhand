# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

add_library(lhandpro_config_cli STATIC lhandpro_config_cli.cpp)
target_compile_features(lhandpro_config_cli PUBLIC cxx_std_17)
target_include_directories(lhandpro_config_cli PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(lhandpro_config_cli PUBLIC dexhand)

add_executable(roboparty-dexhand-config roboparty_dexhand_config_main.cpp)
target_compile_features(roboparty-dexhand-config PRIVATE cxx_std_17)
target_link_libraries(roboparty-dexhand-config PRIVATE lhandpro_config_cli)
set_target_properties(
  roboparty-dexhand-config
  PROPERTIES INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS roboparty-dexhand-config
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
