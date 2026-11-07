// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <fmt/format.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/nx2_timeline.h"

namespace Common::Nx2Timeline {
namespace {

struct Event {
    u64 begin;
    u64 duration;
    const char* name;
};

struct ThreadBuffer {
    std::mutex mutex;
    std::vector<Event> events;
    u64 thread_id{};
    std::string name;
};

// At most this many spans per thread and recording (the GPU threads make ~200,000 a second,
// the front's macros over a million: consecutive spans of one name merge, see Record).
constexpr size_t MaxEvents = size_t{1} << 22;
// Spans of the same name closer than this merge into one.
constexpr u64 MergeGapNs = 2000;

std::mutex g_registry_mutex;
std::vector<std::shared_ptr<ThreadBuffer>> g_buffers;
std::atomic<bool> g_running{false};
std::mutex g_last_mutex;
std::string g_last_file;
u64 g_start_ns{};

thread_local std::shared_ptr<ThreadBuffer> t_buffer;

std::string CurrentThreadName() {
#ifdef _WIN32
    using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    static const auto get_description = reinterpret_cast<GetThreadDescriptionFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),
                                               "GetThreadDescription")));
    if (get_description != nullptr) {
        PWSTR wide = nullptr;
        if (SUCCEEDED(get_description(GetCurrentThread(), &wide)) && wide != nullptr) {
            std::string name;
            for (const wchar_t* c = wide; *c != 0; ++c) {
                name.push_back(*c < 128 ? static_cast<char>(*c) : '?');
            }
            LocalFree(wide);
            if (!name.empty()) {
                return name;
            }
        }
    }
#endif
    return "thread";
}

u64 CurrentThreadId() {
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xffffff;
#endif
}

ThreadBuffer& Buffer() {
    if (!t_buffer) {
        t_buffer = std::make_shared<ThreadBuffer>();
        t_buffer->thread_id = CurrentThreadId();
        t_buffer->name = CurrentThreadName();
        std::scoped_lock lock{g_registry_mutex};
        g_buffers.push_back(t_buffer);
    }
    return *t_buffer;
}

void AppendEscaped(std::string& out, const std::string& text) {
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(static_cast<unsigned char>(c) < 0x20 ? ' ' : c);
    }
}

void Write(const std::filesystem::path& path, double seconds) {
    std::vector<std::shared_ptr<ThreadBuffer>> buffers;
    {
        std::scoped_lock lock{g_registry_mutex};
        buffers = g_buffers;
    }
    std::string json = "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[\n";
    size_t total = 0;
    bool first = true;
    const auto comma = [&] {
        if (!first) {
            json += ",\n";
        }
        first = false;
    };
    for (const auto& buffer : buffers) {
        std::vector<Event> events;
        {
            std::scoped_lock lock{buffer->mutex};
            events.swap(buffer->events);
        }
        if (events.empty()) {
            continue;
        }
        comma();
        json += fmt::format(R"({{"name":"thread_name","ph":"M","pid":1,"tid":{},"args":{{"name":")",
                            buffer->thread_id);
        AppendEscaped(json, buffer->name);
        json += "\"}}";
        for (const Event& event : events) {
            comma();
            json += R"({"name":")";
            AppendEscaped(json, event.name);
            json += fmt::format(R"(","ph":"{}","pid":1,"tid":{},"ts":{:.3f})",
                                event.duration == 0 ? "i" : "X", buffer->thread_id,
                                static_cast<double>(event.begin - g_start_ns) / 1000.0);
            if (event.duration != 0) {
                json += fmt::format(R"(,"dur":{:.3f})", static_cast<double>(event.duration) / 1000.0);
            } else {
                json += R"(,"s":"t")";
            }
            json += '}';
        }
        total += events.size();
    }
    json += "\n]}\n";
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
    file.close();
    LOG_INFO(Debug, "Timeline: {} spans of {} threads over {:.1f} s written to {}", total,
             buffers.size(), seconds, Common::FS::PathToUTF8String(path));
    std::scoped_lock lock{g_last_mutex};
    g_last_file = Common::FS::PathToUTF8String(path);
}

} // Anonymous namespace

u64 Now() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}

void Record(const char* name, u64 begin_ns, u64 end_ns) {
    ThreadBuffer& buffer = Buffer();
    std::scoped_lock lock{buffer.mutex};
    if (!Active()) {
        return;
    }
    if (!buffer.events.empty()) {
        Event& last = buffer.events.back();
        const u64 last_end = last.begin + last.duration;
        if (last.name == name && last.duration != 0 && begin_ns >= last_end &&
            begin_ns - last_end < MergeGapNs) {
            last.duration = end_ns - last.begin;
            return;
        }
    }
    if (buffer.events.size() < MaxEvents) {
        buffer.events.push_back({begin_ns, (std::max)(end_ns - begin_ns, u64{1}), name});
    }
}

void Instant(const char* name) {
    if (!Active()) {
        return;
    }
    ThreadBuffer& buffer = Buffer();
    std::scoped_lock lock{buffer.mutex};
    if (buffer.events.size() < MaxEvents && Active()) {
        buffer.events.push_back({Now(), 0, name});
    }
}

bool Start(const std::filesystem::path& path, double seconds, bool detail) {
    if (g_running.exchange(true)) {
        return false;
    }
    seconds = std::clamp(seconds, 0.1, 30.0);
    {
        std::scoped_lock lock{g_registry_mutex};
        for (const auto& buffer : g_buffers) {
            std::scoped_lock buffer_lock{buffer->mutex};
            buffer->events.clear();
        }
    }
    g_start_ns = Now();
    g_detail.store(detail, std::memory_order_relaxed);
    g_active.store(true, std::memory_order_release);
    LOG_INFO(Debug, "Timeline: recording {:.1f} s", seconds);
    std::thread([path, seconds] {
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
        g_active.store(false, std::memory_order_release);
        // Spans that began before the end finish their Record under their buffer's lock.
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        Write(path, seconds);
        g_running.store(false);
    }).detach();
    return true;
}

std::string LastFile() {
    std::scoped_lock lock{g_last_mutex};
    return g_last_file;
}

void OnPresent() {
    static const char* const directory = std::getenv("EDEN_NX2_TIMELINE");
    if (directory == nullptr) {
        return;
    }
    static const auto first_present = std::chrono::steady_clock::now();
    static bool started = false;
    if (started) {
        return;
    }
    static const double at = [] {
        const char* value = std::getenv("EDEN_NX2_TIMELINE_AT");
        return value != nullptr ? std::atof(value) : 60.0;
    }();
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - first_present).count() <
        at) {
        return;
    }
    started = true;
    const char* value = std::getenv("EDEN_NX2_TIMELINE_SECONDS");
    const double seconds = value != nullptr ? std::atof(value) : 5.0;
    const char* detail = std::getenv("EDEN_NX2_TIMELINE_DETAIL");
    Start(std::filesystem::path{directory} /
              fmt::format("timeline_{}.json", std::chrono::duration_cast<std::chrono::seconds>(
                                                  std::chrono::system_clock::now().time_since_epoch())
                                                  .count()),
          seconds, detail != nullptr && detail[0] == '1');
}

} // namespace Common::Nx2Timeline
