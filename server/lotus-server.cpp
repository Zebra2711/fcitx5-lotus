/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "lotus-server.h"
#include "lotus-logger.h"

#include <vector>

#include <signal.h>
#include <limits.h>
#include <unistd.h>

std::atomic<bool> g_running{true};

FdGuard::~FdGuard() {
    reset();
}

FdGuard::FdGuard(FdGuard&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

FdGuard& FdGuard::operator=(FdGuard&& other) noexcept {
    if (this != &other) {
        reset(other.fd_);
        other.fd_ = -1;
    }
    return *this;
}

void FdGuard::reset(int new_fd) {
    if (fd_ >= 0)
        close(fd_);
    fd_ = new_fd;
}

LibinputContext::LibinputContext(const struct libinput_interface* interface) : udev_(udev_new()) {
    if (udev_ != nullptr) {
        li_ = libinput_udev_create_context(interface, nullptr, udev_);
        if (li_ != nullptr) {
            if (libinput_udev_assign_seat(li_, "seat0") != 0) {
                libinput_unref(li_);
                li_ = nullptr;
            }
        }
    }
}

LibinputContext::~LibinputContext() {
    if (li_ != nullptr)
        libinput_unref(li_);
    if (udev_ != nullptr)
        udev_unref(udev_);
}

void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_running.store(false);
    }
}

std::string get_current_username() {
    struct passwd* pw = getpwuid(getuid());
    return (pw != nullptr) ? pw->pw_name : "unknown";
}

void boost_process_priority() {
    if (setpriority(PRIO_PROCESS, 0, -10) != 0) { //NOLINT
        LotusLogger::instance().error("Failed to boost process priority");
    }
}

void pin_to_pcore() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i <= 3; ++i)
        CPU_SET(i, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        LotusLogger::instance().error("Failed to pin process to core");
    }
}

int open_restricted(const char* path, int flags, void* /*user_data*/) {
    int fd = open(path, flags);
    return fd < 0 ? -errno : fd;
}

void close_restricted(int fd, void* /*user_data*/) {
    close(fd);
}

const struct libinput_interface interface = {
    .open_restricted  = open_restricted,
    .close_restricted = close_restricted,
};

int main(int argc, char* argv[]) {
    std::string target_user;
    if (argc == 3 && strcmp(argv[1], "-u") == 0) { // NOLINT
        target_user = argv[2];                     // NOLINT
    } else {
        target_user = get_current_username();
    }
    LotusLogger::instance().info("Target user: " + target_user);
    boost_process_priority();
    pin_to_pcore();

    std::string mouse_flag_socket;
    mouse_flag_socket.reserve(48);
    mouse_flag_socket += "lotussocket-";
    mouse_flag_socket += target_user;
    mouse_flag_socket += "-mouse_socket";

    const size_t max_socket_path_length = UNIX_PATH_MAX - 1;
    mouse_flag_socket.resize(std::min(mouse_flag_socket.length(), max_socket_path_length));

    FdGuard            mouse_server_fd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0));

    struct sockaddr_un addr_mouse{};
    addr_mouse.sun_family  = AF_UNIX;
    addr_mouse.sun_path[0] = '\0';
    memcpy(&addr_mouse.sun_path[1], mouse_flag_socket.c_str(), mouse_flag_socket.length());

    socklen_t mouse_len = offsetof(struct sockaddr_un, sun_path) + mouse_flag_socket.length() + 1;

    if (bind(mouse_server_fd.get(), (struct sockaddr*)&addr_mouse, mouse_len) != 0) {
        LotusLogger::instance().error("Failed to bind socket");
        return 1;
    }

    listen(mouse_server_fd.get(), 5);

    LibinputContext li_ctx(&interface);
    if (!li_ctx.is_valid()) {
        LotusLogger::instance().error("Failed to create libinput/udev context");
        return 1;
    }

    std::vector<struct pollfd> fds;
    fds.push_back({li_ctx.get_fd(), POLLIN, 0});
    fds.push_back({mouse_server_fd.get(), POLLIN, 0});

    FdGuard          addon_fd;

    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    while (g_running.load(std::memory_order_acquire)) {
        int ret = poll(fds.data(), fds.size(), -1);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        libinput_dispatch(li_ctx.get_li());

        // connect to mouse socket
        if ((fds[1].revents & POLLIN) != 0) {
            int new_fd = accept4(mouse_server_fd.get(), nullptr, nullptr, SOCK_NONBLOCK);
            if (new_fd >= 0) {
                LotusLogger::instance().info("New mouse flag client connected");
                addon_fd.reset(new_fd);
            }
        }

        // handle mouse (libinput)
        if ((fds[0].revents & POLLIN) != 0) {
            struct libinput_event* event = nullptr;

            while ((event = libinput_get_event(li_ctx.get_li())) != nullptr) {
                enum libinput_event_type type = libinput_event_get_type(event);

                if (type == LIBINPUT_EVENT_POINTER_BUTTON) {
                    struct libinput_event_pointer* p = libinput_event_get_pointer_event(event);
                    if (libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED) {
                        if (addon_fd.is_valid()) {
                            if (send(addon_fd.get(), "C", 1, MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) {
                                LotusLogger::instance().warn("Failed to send to mouse flag client, closing connection");
                                addon_fd.reset(-1);
                            }
                        }
                    }
                } else if (type == LIBINPUT_EVENT_DEVICE_ADDED) {
                    struct libinput_device* dev  = libinput_event_get_device(event);
                    const char*             name = libinput_device_get_name(dev);
                    LotusLogger::instance().info("Device added: " + std::string(name));
                    if (libinput_device_config_tap_get_finger_count(dev) > 0) {
                        libinput_device_config_tap_set_enabled(dev, LIBINPUT_CONFIG_TAP_ENABLED);
                        libinput_device_config_tap_set_button_map(dev, LIBINPUT_CONFIG_TAP_MAP_LRM);
                    }
                }
                libinput_event_destroy(event);
            }
        }
    }
    LotusLogger::instance().info("Terminating server...");
    return 0;
}
