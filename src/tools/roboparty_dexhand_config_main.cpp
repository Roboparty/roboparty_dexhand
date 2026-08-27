// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "tools/lhandpro_config_cli.hpp"

#include "drivers/lhandpro/lhandpro_driver.hpp"

#include <iostream>

int main(int argc, const char* argv[]) {
  using namespace roboparty::dexhand::detail;
  return run_lhandpro_config_cli(argc, argv, std::cout, std::cerr,
                                 make_lhandpro_config_driver);
}
