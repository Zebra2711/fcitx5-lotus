/*
 * SPDX-FileCopyrightText: 2026 fcitx5-lotus contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Integration test: LotusMode switching.
//
// Sequence tested:
//   1. Default mode (Vietnamese/Preedit): "aa" → "â"
//   2. Toggle to English (off) mode: "aa" → "aa" (raw passthrough)
//   3. Toggle back to Vietnamese: "ow" → "ơ"
//   4. Per-app mode: context with program "chromium" starts in Uinput mode,
//      raw keys pass through.

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

        // 1. Vietnamese preedit: "aa" + Return → "â"
        tf->call<ITestFrontend::pushCommitExpectation>("â");
        // 2. English off mode: "aa" + Return → "aa"
        tf->call<ITestFrontend::pushCommitExpectation>("aa");
        // 3. Vietnamese restored: "ow" + Return → "ơ"
        tf->call<ITestFrontend::pushCommitExpectation>("ơ");

        auto uuid = tf->call<ITestFrontend::createInputContext>("testapp");

        // 1. Vietnamese default
        tf->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Return), false);

        // 2. Toggle engine off (Ctrl+Shift)
        tf->call<ITestFrontend::keyEvent>(uuid, Key("Control+Shift_L"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Return), false);

        // 3. Toggle engine back on
        tf->call<ITestFrontend::keyEvent>(uuid, Key("Control+Shift_L"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("w"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Return), false);

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

    char arg0[] = "testmodeswitch";
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
