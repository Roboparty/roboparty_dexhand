#pragma once

namespace dh116s::detail {
// Firmware may report the full frame capacity (8 or 31 slots), although
// the connected DH116S has only six active axes. Keep the original layout.
inline bool valid_feedback(unsigned type, unsigned slots, unsigned length) {
    if (slots < 6) return false;
    if (type == 0x50) return slots <= 8 && length >= 2 + 7 * slots;
    if (type == 0x5a) return slots <= 31 && length >= 2 + 2 * slots;
    return false;
}
}
