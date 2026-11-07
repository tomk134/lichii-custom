// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <utility>

#include "common/common_types.h"
#include "common/logging.h"

// Switch 2 GPU-sized compute dispatches. NVN2 games can size a dispatch with words a compute
// shader wrote into the command stream: a 3-word GPFIFO entry that holds the (x, y, z) parameters
// of a compute macro (Ocarina of Time's lighting tile classification, once a frame). Reading them
// made the split's front wait for the host GPU to finish the frame's work so far, with both GPU
// threads idle (~2.3 ms a frame). Instead the front passes placeholder words that name the entry
// (a slot), the macros copy them on unchanged (into the QMD's grid and a constant buffer), and the
// back dispatches indirectly from the entry and copies the entry's words into the constant buffer
// on the GPU. Off by default (see Enabled).
namespace Tegra::Nx2IndirectGrid {

inline constexpr u32 Slots = 4096;

/// Placeholder for word `k` (0-2) of slot `slot`. x: 0x7FC00000 + slot, a grid no dispatch uses.
/// y and z (the QMD's 16-bit fields): 0xC000 + slot * 4 + k. Macros may pass on only x and use
/// constants for y and z.
constexpr u32 Placeholder(u32 slot, u32 k) {
    return k == 0 ? 0x7FC00000u | (slot % Slots) : 0xC000u | ((slot % Slots) << 2) | k;
}

struct State {
    std::array<std::atomic<GPUVAddr>, Slots> addresses{};
    std::atomic<u32> next{};
    std::atomic<bool> used{};
    std::atomic<u64> reserved{};   ///< Counts for the log (LogCounts)
    std::atomic<u64> dispatched{};
    std::atomic<u64> copied{};
    std::atomic<u64> skipped{};
    std::atomic<u64> draws{};          ///< GPU-counted draws
    std::atomic<u64> draws_refused{};  ///< Entries whose macro failed the dry run (read as before)
};

inline State& Get() {
    static State state;
    return state;
}

/// Off by default: Ocarina of Time's lighting flickered white in Kokiri Forest (the stream by
/// Link's house: 144 flash watch reports a minute with it, 1 without, 2026-11-06). Something still
/// races between the entry's write and its two readers. EDEN_NX2_INDIRECT_GRID=1 turns it on.
inline bool Enabled() {
    static const bool enabled = [] {
        const char* const value = std::getenv("EDEN_NX2_INDIRECT_GRID");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

/// GPU-counted draws (below): EDEN_NX2_INDIRECT_DRAWS=1. Off by default: Ocarina's grass went
/// missing and distant plants and shadows flickered with them (2026-11-06).
inline bool DrawsEnabled() {
    static const bool enabled = [] {
        const char* const value = std::getenv("EDEN_NX2_INDIRECT_DRAWS");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

/// Front: a slot for the entry at `address`.
inline u32 Reserve(GPUVAddr address) {
    State& state = Get();
    const u32 slot = state.next.fetch_add(1, std::memory_order_relaxed) % Slots;
    state.addresses[slot].store(address, std::memory_order_release);
    state.used.store(true, std::memory_order_release);
    state.reserved.fetch_add(1, std::memory_order_relaxed);
    return slot;
}

/// Placeholders were ever handed out: a slot stays resolvable until reused 4096 entries later,
/// since a QMD can be dispatched again (dependent launches) after its first dispatch.
inline bool AnyLive() {
    return Get().used.load(std::memory_order_acquire);
}

/// An x placeholder, whether or not its slot resolves.
constexpr bool LooksLikePlaceholder(u32 x) {
    return (x & 0xfffff000u) == 0x7FC00000u;
}

/// The slot and entry address of an x placeholder.
inline std::optional<std::pair<u32, GPUVAddr>> MatchX(u32 x) {
    if (!LooksLikePlaceholder(x)) {
        return std::nullopt;
    }
    const u32 slot = x & (Slots - 1);
    const GPUVAddr address = Get().addresses[slot].load(std::memory_order_acquire);
    if (address == 0) {
        return std::nullopt;
    }
    return std::pair{slot, address};
}

/// GPU-counted draws: a 2-word entry with a 3D macro's (count, unused) parameters, which the macro
/// writes into the draw's vertex count (Ocarina of Time's GPU-culled draws, ~40 a frame). The back
/// draws with vkCmdDrawIndirectByteCountEXT reading the count from the entry (stride 1).
constexpr u32 DrawPlaceholder(u32 slot, u32 k) {
    return (k == 0 ? 0x7FD00000u : 0x7FE00000u) | (slot % Slots);
}

constexpr bool LooksLikeDrawPlaceholder(u32 value) {
    return (value & 0xfffff000u) == 0x7FD00000u || (value & 0xfffff000u) == 0x7FE00000u;
}

/// The entry address behind a draw count placeholder.
inline std::optional<GPUVAddr> MatchDrawCount(u32 value) {
    if ((value & 0xfffff000u) != 0x7FD00000u) {
        return std::nullopt;
    }
    const GPUVAddr address = Get().addresses[value & (Slots - 1)].load(std::memory_order_acquire);
    if (address == 0) {
        return std::nullopt;
    }
    return address;
}

/// Back, at each dispatch: logs the counts every 5 s.
inline void LogCounts() {
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds{5}) {
        return;
    }
    const double seconds = std::chrono::duration<double>(now - last).count();
    last = now;
    State& state = Get();
    LOG_INFO(HW_GPU,
             "GPU-sized work per second: {:.1f} entries passed on, {:.1f} dispatched, {:.1f} "
             "constant buffer copies, {:.1f} skipped, {:.1f} GPU-counted draws, {:.1f} refused",
             state.reserved.exchange(0) / seconds, state.dispatched.exchange(0) / seconds,
             state.copied.exchange(0) / seconds, state.skipped.exchange(0) / seconds,
             state.draws.exchange(0) / seconds, state.draws_refused.exchange(0) / seconds);
}


/// Set while the back writes a QMD to memory: its placeholders stay for the dispatch to match.
inline thread_local bool g_writing_qmd = false;

} // namespace Tegra::Nx2IndirectGrid
