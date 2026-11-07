// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <cstddef>
#include <string>
#include <fmt/format.h>

#include "dynarmic/backend/x64/perf_map.h"
#include "common/common_types.h"
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(_WIN32)
#    include <cstdio>
#    include <cstdlib>
#    include <mutex>
#    ifndef _WIN32
#        include <sys/types.h>
#        include <unistd.h>
#    endif

namespace Dynarmic::Backend::X64 {

namespace {
std::mutex mutex;
std::FILE* file = nullptr;

void OpenFile() {
#ifdef _WIN32
    // EDEN_NX2_JIT_MAP=<file>: where each guest block's host code is, for tools/jitprof.py.
    static const char* const map_path = std::getenv("EDEN_NX2_JIT_MAP");
    if (map_path != nullptr) {
        file = std::fopen(map_path, "w");
    }
    return;
#endif
    const char* perf_dir = std::getenv("PERF_BUILDID_DIR");
    if (!perf_dir) {
        file = nullptr;
        return;
    }

#ifndef _WIN32
    const pid_t pid = getpid();
    const std::string filename = fmt::format("{:s}/perf-{:d}.map", perf_dir, pid);

    file = std::fopen(filename.c_str(), "w");
    if (!file) {
        return;
    }

    std::setvbuf(file, nullptr, _IONBF, 0);
#endif
}
}  // anonymous namespace

namespace detail {
void PerfMapRegister(const void* start, const void* end, std::string_view friendly_name) {
    if (start == end) {
        // Nothing to register
        return;
    }

#ifdef _WIN32
    // Off (the normal case): nothing to do for each block compiled.
    static const bool map_enabled = std::getenv("EDEN_NX2_JIT_MAP") != nullptr;
    if (!map_enabled) {
        return;
    }
#endif
    std::lock_guard guard{mutex};

    if (!file) {
        OpenFile();
        if (!file) {
            return;
        }
    }

    const std::string line = fmt::format("{:016x} {:016x} {:s}\n", reinterpret_cast<u64>(start), reinterpret_cast<u64>(end) - reinterpret_cast<u64>(start), friendly_name);
    std::fwrite(line.data(), sizeof *line.data(), line.size(), file);
    std::fflush(file);
}
}  // namespace detail

void PerfMapClear() {
    std::lock_guard guard{mutex};

    if (!file) {
        return;
    }

    std::fclose(file);
    file = nullptr;
    OpenFile();
}

}  // namespace Dynarmic::Backend::X64

#endif
