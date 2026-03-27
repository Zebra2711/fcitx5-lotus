/*
 * SPDX-FileCopyrightText: 2026 fcitx5-lotus contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Integration test: double-space → period replacement.
// Typing a word, Space, Space should emit "<word>. " (period + space).
// Also verifies that single space does NOT trigger the replacement.

#include "fcitx-utils/eventdispatcher.h"
#include "fcitx-utils/key.h"
#include "fcitx-utils/keysym.h"
#include "fcitx-utils/log.h"
#include "fcitx-utils/macros.h"
#include "fcitx-utils/testing.h"
#include "fcitx/addonmanager.h"
#include "fcitx/instance.h"
#include "testdir.h"
#include "testfrontend_public.h"

using namespace fcitx;

void scheduleEvent(EventDispatcher *dispatcher, Instance *instance) {
    dispatcher->schedule([dispatcher, instance]() {
        auto *tf = instance->addonManager().addon("testfrontend");
        FCITX_ASSERT(tf);

        // Case 1: "xin" + Space + Space → commit "xin. "
        tf->call<ITestFrontend::pushCommitExpectation>("xin. ");

        // Case 2: "chào" + single Space → commit "chào " (no period)
        tf->call<ITestFrontend::pushCommitExpectation>("chào ");

        auto uuid = tf->call<ITestFrontend::createInputContext>("testapp");

        // Case 1
        tf->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_space), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_space), false);

        // Case 2 — new context for isolation
        auto uuid2 = tf->call<ITestFrontend::createInputContext>("testapp");
        tf->call<ITestFrontend::keyEvent>(uuid2, Key("c"), false);
        tf->call<ITestFrontend::keyEvent>(uuid2, Key("h"), false);
        tf->call<ITestFrontend::keyEvent>(uuid2, Key("a"), false);
        tf->call<ITestFrontend::keyEvent>(uuid2, Key("o"), false);  // "chao" → "chào" via telex
        tf->call<ITestFrontend::keyEvent>(uuid2, Key(FcitxKey_space), false);

        dispatcher->schedule([dispatcher, instance]() {
            dispatcher->detach();
            instance->exit();
        });
    });
}

int main() {
    setupTestingEnvironmentPath(
        FCITX5_BINARY_DIR, {"src"},
        {"test", "src/modules", FCITX5_SOURCE_DIR "/test/addon/fcitx5"});

    char arg0[] = "testdoublespace";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,lotus,testui";
    char *argv[] = {arg0, arg1, arg2};

    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    EventDispatcher dispatcher;
    dispatcher.attach(&instance.eventLoop());
    scheduleEvent(&dispatcher, &instance);
    instance.exec();
    return 0;
}
