// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 IcedVodka
// Copyright (C) 2026 wentywenty
// Copyright (C) 2026 cwl
// Copyright (C) 2026 Guangxin YANG

#pragma once
#include "../transport.hpp"

namespace rphand::detail {
class GsUsbWinUsb final : public CanTransport {
public:
    GsUsbWinUsb();
    ~GsUsbWinUsb() override;
    bool open(const ConnectionOptions&) override;
    void close() override;
    bool send(unsigned, const unsigned char*, unsigned, int) override;
    int receive(CanFrame&) override;
    // Read descriptors/capabilities only; does not configure or start CAN.
    static bool inspect(const std::string& serial);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
