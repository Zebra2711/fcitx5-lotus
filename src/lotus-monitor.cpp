/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#include "lotus-monitor.h"
#include "lotus-utils.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <limits.h>

std::thread monitor_thread = std::thread();
std::thread mouse_thread   = std::thread();

void        deletingTimeMonitor() {
    LOTUS_INFO("Deleting monitor thread started.");
    while (!stop_flag_monitor.load()) {
        int64_t deleting_since = 0;

        {
            std::unique_lock<std::mutex> lock(monitor_mutex);
            monitor_cv.wait(lock, [] { return is_deleting_.load(std::memory_order_acquire) || stop_flag_monitor.load(std::memory_order_acquire); });
        }

        if (stop_flag_monitor.load())
            break;

        deleting_since = now_ms();

        while (is_deleting_.load(std::memory_order_acquire) && !stop_flag_monitor.load()) {
            int64_t current_time = now_ms();

            int64_t rep_start = replacement_start_ms_.load(std::memory_order_acquire);
            if (rep_start > 0 && (current_time - rep_start) > 200) {
                LOTUS_WARN("Replacement timeout (200ms). Falling back to commit.");
                int expected_id = replacement_thread_id_.load(std::memory_order_acquire);
                if (expected_id > 0) {
                    is_deleting_.store(false, std::memory_order_release);
                    needFallbackCommit.store(true, std::memory_order_release);
                    replacement_start_ms_.store(0, std::memory_order_release);
                    break;
                }
            }

            if ((current_time - deleting_since) >= 1500) {
                LOTUS_WARN("Critical delete timeout (1500ms). Forcing engine reset.");
                is_deleting_.store(false);
                needEngineReset.store(true);
                replacement_start_ms_.store(0, std::memory_order_release);
                break;
            }

            {
                std::unique_lock<std::mutex> lock(monitor_mutex);
                monitor_cv.wait_for(lock, std::chrono::milliseconds(2));
            }
        }
    }
    monitor_running.store(false, std::memory_order_release);
    LOTUS_INFO("Deleting monitor thread stopped.");
}

void startMonitoring() {
    if (monitor_running.load())
        return;
    if (!monitor_running.exchange(true, std::memory_order_acq_rel)) {
        LOTUS_INFO("Initializing monitor threads...");
        if (monitor_thread.joinable()) {
            monitor_thread.join();
        }
        stop_flag_monitor.store(false, std::memory_order_release);
        monitor_thread = std::thread(deletingTimeMonitor);
    }
}

void mousePressResetThread() {
    const std::string mouse_socket_path = buildSocketPath("mouse_socket");
    LOTUS_INFO("Mouse press reset thread started.");

    while (!stop_flag_monitor.load(std::memory_order_acquire)) {
        int sock = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
        if (sock < 0) {
            LOTUS_ERROR("Failed to create socket: " + std::string(strerror(errno)));
            sleep(1);
            continue;
        }

        struct sockaddr_un addr{};
        addr.sun_family  = AF_UNIX;
        addr.sun_path[0] = '\0';
        memcpy(&addr.sun_path[1], mouse_socket_path.c_str(), mouse_socket_path.length());
        socklen_t len = offsetof(struct sockaddr_un, sun_path) + mouse_socket_path.length() + 1;

        if (connect(sock, (struct sockaddr*)&addr, len) < 0) {
            LOTUS_ERROR("Failed to connect to socket: " + std::string(strerror(errno)));
            close(sock);
            sleep(1);
            continue;
        }
        LOTUS_INFO("Mouse socket connected.");
        mouse_socket_fd.store(sock, std::memory_order_release);

        struct pollfd pfd{};
        pfd.fd     = sock;
        pfd.events = POLLIN;

        while (!stop_flag_monitor.load(std::memory_order_acquire)) {
            int ret = poll(&pfd, 1, -1);

            if (ret > 0 && ((pfd.revents & POLLIN) != 0)) {
                char    buf[16];
                ssize_t n = recv(sock, buf, sizeof(buf), 0);

                if (n <= 0) {
                    LOTUS_ERROR("Mouse socket recv error: " + std::string(strerror(errno)));
                    break;
                }

                struct ucred cred{};
                socklen_t    len                = sizeof(struct ucred);
                char         exe_path[PATH_MAX] = {0};
                if (getsockopt(sock, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
                    char path[64];
                    snprintf(path, sizeof(path), "/proc/%d/cmdline", cred.pid);
                    int fd = open(path, O_RDONLY);
                    if (fd >= 0) {
                        if (read(fd, exe_path, sizeof(exe_path) - 1) < 0) {
                            LOTUS_ERROR("Failed to read cmdline: " + std::string(strerror(errno)));
                        }
                        close(fd);
                    }
                }

                if (strcmp(exe_path, "/usr/bin/fcitx5-lotus-server") == 0) {
                    LOTUS_DEBUG("Mouse click detected from server. Resetting engine.");
                    needEngineReset.store(true, std::memory_order_release);
                    g_mouse_clicked.store(true, std::memory_order_release);
                } else {
                    LOTUS_WARN("Unauthorized connection attempt from: " + std::string(exe_path));
                }
            } else if (ret < 0 && errno != EINTR) {
                LOTUS_ERROR("Mouse socket poll error: " + std::string(strerror(errno)));
                break;
            }
        }
        close(sock);
        mouse_socket_fd.store(-1, std::memory_order_release);
    }
}

void startMouseReset() {
    mouse_thread = std::thread(mousePressResetThread);
}
