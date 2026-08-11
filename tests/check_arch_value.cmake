# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

cmake_minimum_required(VERSION 3.12)
foreach(required IN ITEMS SOURCE_DIR ARCH)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
include("${SOURCE_DIR}/cmake/SelectLHandProSdk.cmake")
roboparty_dexhand_normalize_arch("${ARCH}" ACTUAL)
if(DEFINED EXPECTED AND NOT ACTUAL STREQUAL EXPECTED)
  message(FATAL_ERROR "${ARCH}: expected ${EXPECTED}, got ${ACTUAL}")
endif()
