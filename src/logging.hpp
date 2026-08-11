// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#pragma once

#include <spdlog/spdlog.h>

#include <utility>

namespace roboparty::dexhand::detail {

template <typename Operation>
void with_dexhand_logger(Operation&& operation) noexcept {
  try {
    if (auto logger = spdlog::get("dexhand")) {
      std::forward<Operation>(operation)(*logger);
    }
  } catch (...) {
  }
}

}  // namespace roboparty::dexhand::detail
