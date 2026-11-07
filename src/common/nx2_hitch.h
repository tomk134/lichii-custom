// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <chrono>

#include "common/common_types.h"
#include "common/logging.h"

namespace Common::Nx2Hitch {

// What happened during a long frame: one log line for each frame over 50 ms.
inline std::atomic<u64> g_pipelines{};
inline std::atomic<u64> g_pipeline_us{};
inline std::atomic<u64> g_on_demand_queued{};
inline std::atomic<u64> g_on_demand_built{};
inline std::atomic<u64> g_on_demand_failed{};
inline std::atomic<u64> g_on_demand_us{};
inline std::atomic<u64> g_draws_without_pipeline{};
inline std::atomic<u64> g_file_reads{};
inline std::atomic<u64> g_file_bytes{};
inline std::atomic<u64> g_file_us{};

class FileRead {
public:
    explicit FileRead(s64 size) : begin{std::chrono::steady_clock::now()} {
        g_file_reads.fetch_add(1, std::memory_order_relaxed);
        g_file_bytes.fetch_add(static_cast<u64>(size), std::memory_order_relaxed);
    }
    ~FileRead() {
        g_file_us.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - begin)
                                .count(),
                            std::memory_order_relaxed);
    }

private:
    std::chrono::steady_clock::time_point begin;
};

/// Called once per present with the time since the last one.
inline void NoteFrame(long long frame_us) {
    static u64 pipelines{}, pipeline_us{}, queued{}, reads{}, bytes{}, file_us{};
    const u64 now_pipelines = g_pipelines.load(std::memory_order_relaxed);
    const u64 now_pipeline_us = g_pipeline_us.load(std::memory_order_relaxed);
    const u64 now_queued = g_on_demand_queued.load(std::memory_order_relaxed);
    const u64 now_reads = g_file_reads.load(std::memory_order_relaxed);
    const u64 now_bytes = g_file_bytes.load(std::memory_order_relaxed);
    const u64 now_file_us = g_file_us.load(std::memory_order_relaxed);
    if (frame_us > 50'000) {
        LOG_INFO(Service,
                 "Hitch: {:.1f} ms frame; GPU thread built {} pipelines in {:.1f} ms, {} queued on "
                 "demand; {} file reads, {:.1f} MiB, {:.1f} ms; ends at {} ns",
                 static_cast<double>(frame_us) / 1000.0, now_pipelines - pipelines,
                 static_cast<double>(now_pipeline_us - pipeline_us) / 1000.0, now_queued - queued,
                 now_reads - reads, static_cast<double>(now_bytes - bytes) / 1048576.0,
                 static_cast<double>(now_file_us - file_us) / 1000.0,
                 std::chrono::steady_clock::now().time_since_epoch().count());
    }
    pipelines = now_pipelines;
    pipeline_us = now_pipeline_us;
    queued = now_queued;
    reads = now_reads;
    bytes = now_bytes;
    file_us = now_file_us;
}

} // namespace Common::Nx2Hitch
