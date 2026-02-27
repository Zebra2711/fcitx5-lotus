/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-utils.h"

#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/utf8.h>

#include <chrono>
#include <cstdlib>
#include <cstring>

// Global variables
fcitx::LotusMode        realMode = fcitx::LotusMode::Smooth;
std::atomic<bool>       needEngineReset{false};
std::string             BASE_SOCKET_PATH;
std::atomic<bool>       g_mouse_clicked{false};
std::atomic<bool>       is_deleting_{false};
const int               MAX_BACKSPACE_COUNT = 8;
std::once_flag          monitor_init_flag;
std::atomic<bool>       stop_flag_monitor{false};
std::atomic<bool>       monitor_running{false};
int                     uinput_client_fd_ = -1;
int                     realtextLen       = 0;
bool                    waitAck           = false;
std::atomic<int>        mouse_socket_fd{-1};

std::atomic<int64_t>    replacement_start_ms_{0};
std::atomic<int>        replacement_thread_id_{0};
std::atomic<bool>       needFallbackCommit{false};

std::mutex              monitor_mutex;
std::condition_variable monitor_cv;

std::string             buildSocketPath(const char* base_path_suffix) {
    const char* username_c = std::getenv("USER");
    std::string path;
    path.reserve(32);
    path += "lotussocket-";
    path += (username_c ? username_c : "default");
    path += '-';
    path += base_path_suffix;
    return path;
}

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool isBackspace(uint32_t sym) {
    return sym == 65288 || sym == 8 || sym == FcitxKey_BackSpace;
}

std::string SubstrChar(const std::string& s, size_t start, size_t len) {
    if (s.empty())
        return "";
    const char* start_ptr = fcitx_utf8_get_nth_char(s.c_str(), static_cast<uint32_t>(start));
    if (*start_ptr == '\0')
        return "";
    if (len == std::string::npos)
        return std::string(start_ptr);
    const char* end_ptr = fcitx_utf8_get_nth_char(start_ptr, static_cast<uint32_t>(len));
    return std::string(start_ptr, end_ptr - start_ptr);
}

int compareAndSplitStrings(const std::string& A, const std::string& B, std::string& commonPrefix, std::string& deletedPart, std::string& addedPart) {
    const char* ptrA   = A.c_str();
    const char* ptrB   = B.c_str();
    const char* endA   = ptrA + A.size();
    const char* endB   = ptrB + B.size();
    const char* startA = ptrA;

    while (ptrA < endA && ptrB < endB) {
        unsigned int lenA = fcitx_utf8_char_len(ptrA);
        unsigned int lenB = fcitx_utf8_char_len(ptrB);
        if (lenA == lenB && std::strncmp(ptrA, ptrB, lenA) == 0) {
            ptrA += lenA;
            ptrB += lenB;
        } else {
            break;
        }
    }

    commonPrefix.assign(startA, ptrA);
    deletedPart.assign(ptrA, endA);
    addedPart.assign(ptrB, endB);
    return (deletedPart.empty() && addedPart.empty()) ? 1 : 2;
}
