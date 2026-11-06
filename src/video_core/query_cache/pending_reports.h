// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>

#include "common/common_types.h"

/// Counts the reports the query cache holds whose value is not in guest memory yet, per 16
/// bytes of device memory (hashed, so a place can read as holding one when it has none, never the
/// reverse). The split's front asks this before a macro reads report memory: with none held
/// there is nothing to flush, so it need not wait for the back. Ocarina made ~12,000 such waits a
/// second standing still and ~20,000 with the camera turning; under 100 found a report to wait
/// for, the rest only copied a finished value (QueryCacheBase::WriteFinalReports does that as
/// the host results arrive instead).
namespace VideoCommon::PendingReports {

inline std::array<std::atomic<u32>, 65536> g_granules{};
/// Too many reports tracked: every place counts as holding one.
inline std::atomic<bool> g_overflow{};

inline std::atomic<u32>& Slot(u64 address) {
    return g_granules[static_cast<size_t>(((address >> 4) * 0x9E3779B97F4A7C15ULL) >> 48)];
}

inline void Add(u64 address) {
    Slot(address).fetch_add(1, std::memory_order_seq_cst);
}

inline void Remove(u64 address) {
    auto& slot = Slot(address);
    u32 held = slot.load(std::memory_order_seq_cst);
    while (held != 0 && !slot.compare_exchange_weak(held, held - 1, std::memory_order_seq_cst)) {
    }
}

inline void Reset() {
    for (auto& slot : g_granules) {
        slot.store(0, std::memory_order_relaxed);
    }
    g_overflow.store(false, std::memory_order_relaxed);
}

/// Whether a report may be held for [address, address + size) (device addresses). A report is
/// counted at its first byte and is up to 16 bytes long.
[[nodiscard]] inline bool Any(u64 address, u64 size) {
    if (g_overflow.load(std::memory_order_relaxed)) {
        return true;
    }
    const u64 last = (address + size - 1) >> 4;
    for (u64 granule = (address >= 16 ? address - 16 : 0) >> 4; granule <= last; ++granule) {
        if (Slot(granule << 4).load(std::memory_order_seq_cst) != 0) {
            return true;
        }
    }
    return false;
}

} // namespace VideoCommon::PendingReports
