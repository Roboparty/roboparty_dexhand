// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "drivers/lhandpro/lhandpro_sdk.hpp"
#include "test_support.hpp"

#include <type_traits>

using roboparty::dexhand::detail::CapiLHandProSdk;

static_assert(!std::is_copy_constructible_v<CapiLHandProSdk>);
static_assert(!std::is_copy_assignable_v<CapiLHandProSdk>);
static_assert(!std::is_move_constructible_v<CapiLHandProSdk>);
static_assert(!std::is_move_assignable_v<CapiLHandProSdk>);

int main() {
  CapiLHandProSdk sdk;
  CHECK(sdk.create());
  CHECK_EQ(sdk.set_hand_type(2), 0);
  int model = -1;
  CHECK_EQ(sdk.get_hand_type(model), 0);
  CHECK_EQ(model, 2);
  sdk.destroy();
  sdk.destroy();
  return 0;
}
