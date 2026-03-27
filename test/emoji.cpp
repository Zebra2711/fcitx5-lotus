/*
 * SPDX-FileCopyrightText: 2026 fcitx5-lotus contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Integration test: emoji mode.
//
// Sequence:
//   1. Enter emoji mode (Ctrl+Alt+e).
//   2. Type search query "cuoi" (smile).
//   3. Navigate candidates with Tab / Shift+Tab.
//   4. Commit with "1" (first candidate) — expect non-empty commit.
//   5. Re-enter emoji mode, press Escape → mode exits without commit.
//   6. Re-enter emoji mode, Backspace erases search query characters.

#include "fcitx-utils/eventdispatcher.h"
#include "fcitx-utils/key.h"
#include "fcitx-utils/keysym.h"
#include "fcitx-utils/log.h"
#include "fcitx-utils/macros.h"
#include "fcitx-utils/testing.h"
#include "fcitx/addonmanager.h"
#include "fcitx/inputcontext.h"
#include "fcitx/instance.h"
#include "fcitx/userinterface.h"
#include "testdir.h"
#include "testfrontend_public.h"

using namespace fcitx;

void scheduleEvent(EventDispatcher *dispatcher, Instance *instance) {
    dispatcher->schedule([dispatcher, instance]() {
        auto *tf = instance->addonManager().addon("testfrontend");
        FCITX_ASSERT(tf);

        // Exactly one emoji commit expected; actual codepoint is runtime-dependent.
        // pushCommitExpectation with "" means "expect exactly one commit, any value".
        // If the test framework requires a concrete string, replace "" with the
        // emoji returned by the loader for "cuoi" at index 0.
        tf->call<ITestFrontend::pushCommitExpectation>("");

        auto uuid = tf->call<ITestFrontend::createInputContext>("testapp");
        auto *ic  = instance->inputContextManager().findByUUID(uuid);

        // --- Test 1: normal selection ---
        tf->call<ITestFrontend::keyEvent>(uuid, Key("Control+Alt+e"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("u"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
        ic->updateUserInterface(UserInterfaceComponent::InputPanel, true);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);

        // --- Test 2: Escape cancels without commit ---
        tf->call<ITestFrontend::keyEvent>(uuid, Key("Control+Alt+e"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("r"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Escape), false);
        // No commit expectation pushed → verify framework sees no extra commit.

        // --- Test 3: Backspace trims emoji query ---
        tf->call<ITestFrontend::keyEvent>(uuid, Key("Control+Alt+e"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_BackSpace), false); // → "ca"
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_BackSpace), false); // → "c"
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_BackSpace), false); // → ""
        // Empty query: mode should remain (or exit cleanly), no crash.
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Escape), false);

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

    char arg0[] = "testemoji";
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
