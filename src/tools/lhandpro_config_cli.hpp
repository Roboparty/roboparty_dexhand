// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#pragma once

#include <functional>
#include <iosfwd>
#include <memory>
#include <string>

class LHandProDriver;

namespace roboparty::dexhand::detail {

using ConfigDriverFactory = std::function<std::unique_ptr<LHandProDriver>(
    const std::string&, int)>;

int run_lhandpro_config_cli(int argc, const char* const argv[],
                            std::ostream& output, std::ostream& error,
                            const ConfigDriverFactory& factory);

std::unique_ptr<LHandProDriver> make_lhandpro_config_driver(
    const std::string& interface, int node_id);

}  // namespace roboparty::dexhand::detail
