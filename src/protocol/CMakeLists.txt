# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

add_library(dexhand_canfd STATIC socket_canfd_transport.cpp)
target_compile_features(dexhand_canfd PUBLIC cxx_std_17)
set_target_properties(dexhand_canfd PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(dexhand_canfd PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dexhand_canfd PUBLIC Threads::Threads spdlog::spdlog)
