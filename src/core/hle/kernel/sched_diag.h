// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "common/common_types.h"

// TEMPORARY (perf investigation), EDEN_NX2_THREAD_DIAG=1: where guest threads spend the time
// they are not running. Per thread id: how long it sat runnable before a core took it, and how
// long it was blocked, by wait reason.
namespace Kernel::SchedDiag {

struct Slot {
    std::atomic<s64> ready_since{};
    std::atomic<s64> blocked_since{};
    std::atomic<s64> ready_ns{};
    std::atomic<s64> ready_count{};
    std::atomic<s64> ready_max_ns{};
    std::array<std::atomic<s64>, 8> blocked_ns{};
    std::array<std::atomic<s64>, 8> blocked_count{};
};

inline std::array<Slot, 1024> g_slots;

inline bool Enabled() {
    static const bool enabled = std::getenv("EDEN_NX2_THREAD_DIAG") != nullptr;
    return enabled;
}

inline s64 Now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline Slot& For(u64 thread_id) {
    return g_slots[thread_id % g_slots.size()];
}

// EDEN_NX2_SCHED_TRACE=<start s>,<seconds>: every thread switch and wake-up in that window of
// the process's run, written to sched_trace.txt in the working directory.
struct Event {
    s64 time;
    u32 a;
    u32 b;
    u32 kind; ///< 0: core `a` switches to thread `b`; 1: thread `a` wakes `b`; 2: `a` blocks
};

struct Trace {
    std::vector<Event> events;
    std::atomic<size_t> count{};
    s64 begin{};
    s64 end{};
    std::atomic<bool> written{};
    Trace() {
        const char* const text = std::getenv("EDEN_NX2_SCHED_TRACE");
        if (text == nullptr) {
            return;
        }
        double start = 0;
        double seconds = 0;
        if (std::sscanf(text, "%lf,%lf", &start, &seconds) != 2) {
            return;
        }
        events.resize(size_t{1} << 22);
        begin = Now() + static_cast<s64>(start * 1e9);
        end = begin + static_cast<s64>(seconds * 1e9);
    }
    static s64 Now() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    void Add(u32 kind, u32 a, u32 b) {
        if (end == 0) {
            return;
        }
        const s64 now = Now();
        if (now < begin) {
            return;
        }
        if (now >= end) {
            if (!written.exchange(true)) {
                if (std::FILE* const file = std::fopen("sched_trace.txt", "w")) {
                    const size_t total = (std::min)(count.load(), events.size());
                    for (size_t i = 0; i < total; ++i) {
                        std::fprintf(file, "%lld %u %u %u\n",
                                     static_cast<long long>(events[i].time - begin),
                                     events[i].kind, events[i].a, events[i].b);
                    }
                    std::fclose(file);
                }
            }
            return;
        }
        if (const size_t index = count.fetch_add(1); index < events.size()) {
            events[index] = {now, a, b, kind};
        }
    }
};

inline Trace g_trace;

/// The thread became runnable after waiting for `reason`.
inline void OnRunnable(u64 thread_id, u32 reason) {
    Slot& slot = For(thread_id);
    const s64 now = Now();
    if (const s64 since = slot.blocked_since.exchange(0); since != 0) {
        slot.blocked_ns[reason & 7].fetch_add(now - since, std::memory_order_relaxed);
        slot.blocked_count[reason & 7].fetch_add(1, std::memory_order_relaxed);
    }
    slot.ready_since.store(now, std::memory_order_relaxed);
}

/// The thread stopped being runnable (it waits).
inline void OnBlocked(u64 thread_id) {
    Slot& slot = For(thread_id);
    slot.ready_since.store(0, std::memory_order_relaxed);
    slot.blocked_since.store(Now(), std::memory_order_relaxed);
}

/// A core starts running the thread.
inline void OnSwitchIn(u64 thread_id) {
    Slot& slot = For(thread_id);
    if (const s64 since = slot.ready_since.exchange(0); since != 0) {
        const s64 waited = Now() - since;
        slot.ready_ns.fetch_add(waited, std::memory_order_relaxed);
        slot.ready_count.fetch_add(1, std::memory_order_relaxed);
        if (waited > slot.ready_max_ns.load(std::memory_order_relaxed)) {
            slot.ready_max_ns.store(waited, std::memory_order_relaxed);
        }
    }
}

/// A core stops running the thread while it is still runnable (preempted).
inline void OnSwitchOutRunnable(u64 thread_id) {
    For(thread_id).ready_since.store(Now(), std::memory_order_relaxed);
}

} // namespace Kernel::SchedDiag
