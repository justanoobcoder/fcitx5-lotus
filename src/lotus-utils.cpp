/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-utils.h"
#include "lotus-config.h"

#include <cstddef>
#include <fcitx-utils/utf8.h>
#include <pwd.h>
#include <unistd.h>

#include <algorithm>

// Global variables
std::atomic<fcitx::LotusMode> realMode{fcitx::LotusMode::Smooth};
std::atomic<bool>             needEngineReset{false};
std::atomic<bool>             g_mouse_clicked{false};
std::atomic<bool>             is_deleting_{false};
std::atomic<bool>             stop_flag_monitor{false};
std::atomic<bool>             monitor_running{false};
std::atomic<int>              uinput_client_fd_{-1};
std::atomic<unsigned int>     realtextLen{0};
std::atomic<int>              mouse_socket_fd{-1};

std::atomic<int64_t>          replacement_start_ms_{0};
std::atomic<int>              replacement_thread_id_{0};
std::atomic<bool>             needFallbackCommit{false};

std::mutex                    monitor_mutex;
std::condition_variable       monitor_cv;

FCITX_DEFINE_LOG_CATEGORY(lotus, "lotus", fcitx::LogLevel::NoLog);

std::string buildSocketPath(const char* base_path_suffix) {
    struct passwd  pwd{};
    struct passwd* result   = nullptr;
    long           buf_size = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (buf_size == -1) {
        buf_size = 16384;
    }
    std::vector<char> buf(buf_size);
    std::string       username;
    int               res = getpwuid_r(getuid(), &pwd, buf.data(), buf_size, &result);
    if (res == 0 && result != nullptr) {
        username = result->pw_name;
    } else {
        username = "unknown";
    }
    std::string path;
    path.reserve(32);
    path += "lotussocket-";
    path += username;
    path += '-';
    path += base_path_suffix;
    const size_t max_socket_path_length = UNIX_PATH_MAX - 1;
    path.resize(std::min(path.length(), max_socket_path_length));
    return path;
}

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool isBackspace(uint32_t sym) {
    return sym == 65288 || sym == 8 || sym == FcitxKey_BackSpace;
}

int compareAndSplitStrings(const std::string& A, const std::string& B, std::string& deletedPart, std::string& addedPart) {
    size_t minLen   = std::min(A.size(), B.size());
    size_t startPos = (minLen > 20) ? (minLen - 20) : 0;

    while (startPos > 0 && (static_cast<unsigned char>(A[startPos]) & 0xC0) == 0x80) {
        --startPos;
    }

    auto [itA, itB] = std::mismatch(A.begin() + static_cast<std::ptrdiff_t>(startPos), A.end(), B.begin() + static_cast<std::ptrdiff_t>(startPos), B.end());

    size_t splitPoint = 0;
    if (itA == A.end()) {
        splitPoint = minLen;
    } else {
        size_t pos = std::distance(A.begin(), itA);
        splitPoint = startPos + pos;
        while (splitPoint > 0 && (static_cast<unsigned char>(A[splitPoint]) & 0xC0) == 0x80) {
            --splitPoint;
        }
    }

    deletedPart.assign(A, splitPoint);
    addedPart.assign(B, splitPoint);

    return (deletedPart.empty() && addedPart.empty()) ? 1 : 2;
}

bool isStartsWith(const std::string& str, const std::string& prefix) {
#if __cplusplus >= 202002L
    return str.starts_with(prefix);
#else
    return str.substr(0, prefix.size()) == prefix;
#endif
}

std::string getFrontendName(fcitx::InputContext* ic) {
    if (ic == nullptr) {
        return "unknown";
    }
    return ic->frontend();
}