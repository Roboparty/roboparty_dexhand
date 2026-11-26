// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 IcedVodka
// Copyright (C) 2026 wentywenty
// Copyright (C) 2026 cwl
// Copyright (C) 2026 Guangxin YANG

#pragma once

#include "../transport.hpp"
#include "interface_lock.hpp"
#include <string>

namespace rphand::detail {
class SocketCan : public CanTransport {
public:
    ~SocketCan() override;
    bool open(const ConnectionOptions& options) override;
    void close() override;
    bool send(unsigned id, const unsigned char* data, unsigned size, int extended) override;
    // 1 = frame, 0 = poll timeout/interruption, -1 = transport failure.
    int receive(CanFrame& frame) override;
private:
    int fd_ = -1;
    InterfaceLock interface_lock_;
};
}  // namespace rphand::detail
