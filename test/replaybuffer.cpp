/*
 * SPDX-FileCopyrightText: 2026 fcitx5-lotus contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Integration test: buffered key replay.
//
// When is_deleting_ is true (replacement in flight), keystrokes typed by
// the user are buffered (up to MAX_BUFFERED_KEYS=50) and replayed once the
// deletion completes.  This test types rapidly while a replacement is pending
// and verifies that no keystrokes are silently dropped.
//
// Strategy: type a word that triggers telex replacement ("oo" → "ô"), then
// immediately queue more characters before the replacement finishes.
// The final committed string must contain all typed characters.

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

        // "cô bé" — typed as c-o-o-space-b-e, "oo"→"ô" triggers replacement.
        // Extra characters "space b e" must be replayed after replacement.
        tf->call<ITestFrontend::pushCommitExpectation>("cô bé");

        auto uuid = tf->call<ITestFrontend::createInputContext>("testapp");

        tf->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("o"), false); // triggers "ô" replacement
        // These arrive while replacement may still be in flight:
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_space), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Return), false);

        // Stress: fill the buffer to MAX_BUFFERED_KEYS-1 (49 keys)
        // Engine should not crash; oldest keys may be dropped gracefully.
        tf->call<ITestFrontend::pushCommitExpectation>("");  // any commit is fine
        auto uuid2 = tf->call<ITestFrontend::createInputContext>("testapp");
        tf->call<ITestFrontend::keyEvent>(uuid2, Key("a"), false);
        tf->call<ITestFrontend::keyEvent>(uuid2, Key("a"), false); // "aa"→"â" replacement
        for (int i = 0; i < 49; ++i) {
            tf->call<ITestFrontend::keyEvent>(uuid2, Key("x"), false);
        }
        tf->call<ITestFrontend::keyEvent>(uuid2, Key(FcitxKey_Return), false);

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

    char arg0[] = "testreplaybuffer";
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
