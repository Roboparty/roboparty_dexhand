// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include <hand_driver.hpp>

int main() {
  auto hand = HandDriver::create_hand("LHandPro", "canfd", "can0");
  return hand && hand->get_can_name() == "can0" ? 0 : 1;
}
