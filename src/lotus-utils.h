/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/**
 * @file lotus-utils.h
 * @brief Utility functions and global state for fcitx5-lotus.
 */

#ifndef _FCITX5_LOTUS_UTILS_H_
#define _FCITX5_LOTUS_UTILS_H_

#include "lotus-config.h"
#include <fcitx-utils/utf8.h>
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>

// Forward declaration for fcitx types
typedef uint32_t KeySym;

namespace fcitx {
    enum class LotusMode;
}

// Global state variables for input processing
extern fcitx::LotusMode        realMode;               ///< Current active input mode
extern std::atomic<bool>       needEngineReset;        ///< Flag to trigger engine reset
extern std::string             BASE_SOCKET_PATH;       ///< Base path for Unix sockets
extern std::atomic<bool>       g_mouse_clicked;        ///< Mouse click detection flag
extern std::atomic<bool>       is_deleting_;           ///< Deletion in progress flag
extern const int               MAX_BACKSPACE_COUNT;    ///< Maximum backspace limit
extern std::once_flag          monitor_init_flag;      ///< One-time initialization flag
extern std::atomic<bool>       stop_flag_monitor;      ///< Signal to stop monitor threads
extern std::atomic<bool>       monitor_running;        ///< Monitor thread status
extern int                     uinput_client_fd_;      ///< Uinput client file descriptor
extern int                     realtextLen;            ///< Current text length
extern bool                    waitAck;                ///< Waiting for acknowledgment
extern std::atomic<int>        mouse_socket_fd;        ///< Mouse socket file descriptor
extern std::atomic<int64_t>    replacement_start_ms_;  ///< Timestamp for replacement
extern std::atomic<int>        replacement_thread_id_; ///< Thread ID for replacement
extern std::atomic<bool>       needFallbackCommit;     ///< Fallback commit flag
extern std::mutex              monitor_mutex;          ///< Mutex for monitor synchronization
extern std::condition_variable monitor_cv;             ///< Condition variable for monitor

/**
 * @brief Builds socket path from base suffix.
 * @param base_path_suffix Suffix to append to base path.
 * @return Full socket path.
 */
std::string buildSocketPath(const char* base_path_suffix);

/**
 * @brief Gets current time in milliseconds.
 * @return Timestamp in milliseconds.
 */
int64_t now_ms();

/**
 * @brief Checks if key symbol is a backspace.
 * @param sym Key symbol to check.
 * @return True if backspace.
 */
bool isBackspace(uint32_t sym);

/**
 * @brief Extracts substring by character count.
 * @param s Source string.
 * @param start Start character index.
 * @param len Number of characters.
 * @return Substring.
 */
std::string SubstrChar(const std::string& s, size_t start, size_t len);

/**
 * @brief Compares two strings and computes diff.
 * @param A First string.
 * @param B Second string.
 * @param commonPrefix Output common prefix.
 * @param deletedPart Output deleted portion.
 * @param addedPart Output added portion.
 * @return Comparison result code.
 */
int compareAndSplitStrings(const std::string& A, const std::string& B, std::string& commonPrefix, std::string& deletedPart, std::string& addedPart);

/**
 * @brief Key event entry for replay buffer.
 */
struct KeyEntry {
    uint32_t sym;   ///< Key symbol
    uint32_t state; ///< Key state (modifiers)
};

#endif // _FCITX5_LOTUS_UTILS_H_
