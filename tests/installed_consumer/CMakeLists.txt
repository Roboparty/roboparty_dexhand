# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

cmake_minimum_required(VERSION 3.12)
project(dexhand_installed_consumer LANGUAGES CXX)

find_package(roboparty_dexhand CONFIG REQUIRED)

add_executable(dexhand_consumer main.cpp)
target_compile_features(dexhand_consumer PRIVATE cxx_std_17)
target_link_libraries(dexhand_consumer PRIVATE roboparty_dexhand::dexhand)
