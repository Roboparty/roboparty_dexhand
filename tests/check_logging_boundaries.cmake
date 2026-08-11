# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

foreach(source IN ITEMS
    "${SOURCE_DIR}/src/protocol/socket_canfd_transport.cpp"
    "${SOURCE_DIR}/src/drivers/lhandpro/lhandpro_driver.cpp")
  file(READ "${source}" contents)
  if(contents MATCHES "spdlog::(trace|debug|info|warn|error|critical|log)[ \t\r\n]*\\(")
    message(FATAL_ERROR "${source} contains a free spdlog call")
  endif()
  if(contents MATCHES "spdlog::default_logger")
    message(FATAL_ERROR "${source} reaches the process default logger")
  endif()
endforeach()
