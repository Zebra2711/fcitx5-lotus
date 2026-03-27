/*
 * SPDX-FileCopyrightText: 2026 fcitx5-lotus contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Unit tests for ack_apps list (ack-apps.h).
// Verifies ACK-workaround membership for Chromium-based browsers
// and correct exclusion of non-ACK apps.

#include "fcitx-utils/log.h"
#include "ack-apps.h"
#include <algorithm>
#include <string>

static bool isAckApp(const std::string &name) {
    return std::find(ack_apps.begin(), ack_apps.end(), name) != ack_apps.end();
}

static void test_known_ack_apps() {
    // All Chromium-based browsers that require the workaround
    for (const auto *app : {"chrome", "chromium", "brave", "edge",
                             "vivaldi", "opera", "coccoc", "cromite",
                             "helium", "thorium", "slimjet", "yandex"}) {
        FCITX_ASSERT(isAckApp(app));
    }
}

static void test_non_ack_apps() {
    // Non-Chromium apps must NOT be in the list
    for (const auto *app : {"firefox", "gedit", "kate", "konsole",
                             "gnome-terminal", "code", "nvim", ""}) {
        FCITX_ASSERT(!isAckApp(app));
    }
}

static void test_case_sensitivity() {
    // List uses lowercase; uppercase variants must not match
    FCITX_ASSERT(!isAckApp("Chrome"));
    FCITX_ASSERT(!isAckApp("CHROME"));
    FCITX_ASSERT(!isAckApp("Chromium"));
}

static void test_no_duplicates() {
    for (const auto &app : ack_apps) {
        int count = std::count(ack_apps.begin(), ack_apps.end(), app);
        FCITX_ASSERT(count == 1);
    }
}

int main() {
    test_known_ack_apps();
    test_non_ack_apps();
    test_case_sensitivity();
    test_no_duplicates();
    return 0;
}
