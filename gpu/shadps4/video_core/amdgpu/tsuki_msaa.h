// SPDX-License-Identifier: GPL-2.0-or-later
// gow3: guest multisampled images created and sampled as single-sampled ones.
// Guest sizes and addresses still use the real sample count (GuestNumSamples).
//
// On with AMD's and Intel's Windows drivers (tsuki_msaa_default.h); TSUKI_NO_MSAA=0 or 1 decides
// otherwise (vk_instance.cpp).

#pragma once

#include <atomic>

namespace AmdGpu {

inline std::atomic<bool> no_msaa{false};

inline bool NoMsaa() {
    return no_msaa.load(std::memory_order_relaxed);
}

} // namespace AmdGpu
