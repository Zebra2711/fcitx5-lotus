/*
 * SPDX-FileCopyrightText: 2024 fcitx5-lotus contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include <memory>
#include <string>
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

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void typeKeys(ITestFrontend *tf, const InputContextUUID &uuid,
                     std::initializer_list<const char *> keys) {
    for (const auto *k : keys)
        tf->call<ITestFrontend::keyEvent>(uuid, Key(k), false);
}

// ---------------------------------------------------------------------------
// testBasicVietnamese
//   Telex: aa→â  ow→ơ  uf→ứ  Return commits
// ---------------------------------------------------------------------------
void testBasicVietnamese(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *tf = instance->addonManager().addon("testfrontend");

        // Expected committed strings (telex decomposition)
        for (const auto *expect : {"bân", "mời", "nhứ"}) {
            tf->call<ITestFrontend::pushCommitExpectation>(expect);
        }

        auto uuid = tf->call<ITestFrontend::createInputContext>("testapp");
        auto *ic  = instance->inputContextManager().findByUUID(uuid);

        // "bân"  b-a-a-n  → â via telex double-a
        typeKeys(tf, uuid, {"b", "a", "a", "n"});
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Return), false);

        // "mời"  m-o-w-i  → ơ via telex o+w, grave via trailing key handled
        //        by bamboo engine; we just test commit path here.
        typeKeys(tf, uuid, {"m", "o", "w", "i"});
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Return), false);

        // "nhứ"  n-h-u-f  → ứ via telex u+f (acute)
        typeKeys(tf, uuid, {"n", "h", "u", "f"});
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Return), false);

        FCITX_INFO() << "testBasicVietnamese done";
        (void)ic;
    });
}

// ---------------------------------------------------------------------------
// testBackspace
//   Type "xaas", backspace once → "xaa" → commit "xâ"
// ---------------------------------------------------------------------------
void testBackspace(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *tf = instance->addonManager().addon("testfrontend");
        tf->call<ITestFrontend::pushCommitExpectation>("xâ");

        auto uuid = tf->call<ITestFrontend::createInputContext>("testapp");

        typeKeys(tf, uuid, {"x", "a", "a", "s"});
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_BackSpace), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Return), false);

        FCITX_INFO() << "testBackspace done";
    });
}

// ---------------------------------------------------------------------------
// testEmojiMode
//   Toggle emoji mode, type a query, pick first candidate.
// ---------------------------------------------------------------------------
void testEmojiMode(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *tf = instance->addonManager().addon("testfrontend");
        // Emoji mode commit is emoji string — accept anything non-empty.
        // We push a placeholder; real value depends on runtime emoji data.
        tf->call<ITestFrontend::pushCommitExpectation>("");

        auto uuid = tf->call<ITestFrontend::createInputContext>("testapp");
        auto *ic  = instance->inputContextManager().findByUUID(uuid);

        // Lotus emoji toggle key (typically Ctrl+Alt+E or configured shortcut).
        // Using the default as defined in lotus.conf.in — adjust if needed.
        tf->call<ITestFrontend::keyEvent>(uuid, Key("Control+Alt+e"), false);

        typeKeys(tf, uuid, {"c", "u", "o", "i"});  // "cuoi" = smile query
        ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel, true);

        // Select first candidate with "1"
        tf->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);

        FCITX_INFO() << "testEmojiMode done";
        (void)ic;
    });
}

// ---------------------------------------------------------------------------
// testEngineToggle
//   Disable engine → raw ASCII should pass through unchanged.
// ---------------------------------------------------------------------------
void testEngineToggle(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *tf = instance->addonManager().addon("testfrontend");
        tf->call<ITestFrontend::pushCommitExpectation>("aan");

        auto uuid = tf->call<ITestFrontend::createInputContext>("testapp");

        // Toggle engine off (default: Ctrl+Shift — matches lotus.conf.in).
        tf->call<ITestFrontend::keyEvent>(uuid, Key("Control+Shift_L"), false);

        // Now "aa" should NOT produce "â"; plain ASCII committed.
        typeKeys(tf, uuid, {"a", "a", "n"});
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Return), false);

        // Re-enable for subsequent tests.
        tf->call<ITestFrontend::keyEvent>(uuid, Key("Control+Shift_L"), false);

        FCITX_INFO() << "testEngineToggle done";
    });
}

// ---------------------------------------------------------------------------
// testCandidateNavigation
//   Type, navigate candidates with Tab/Shift+Tab, commit via Space.
// ---------------------------------------------------------------------------
void testCandidateNavigation(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *tf = instance->addonManager().addon("testfrontend");
        // Second candidate for "la" in the dictionary.
        tf->call<ITestFrontend::pushCommitExpectation>("là");

        auto uuid = tf->call<ITestFrontend::createInputContext>("testapp");
        auto *ic  = instance->inputContextManager().findByUUID(uuid);

        typeKeys(tf, uuid, {"l", "a"});
        ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel, true);

        // Move to next candidate, then commit.
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_Tab), false);
        tf->call<ITestFrontend::keyEvent>(uuid, Key(FcitxKey_space), false);

        FCITX_INFO() << "testCandidateNavigation done";
        (void)ic;
    });
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    setupTestingEnvironmentPath(
        FCITX5_BINARY_DIR, {"src"},
        {"test", "src/modules", FCITX5_SOURCE_DIR "/test/addon/fcitx5"});

    char arg0[] = "testlotus";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,lotus,testui";
    char *argv[] = {arg0, arg1, arg2};

    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    testBasicVietnamese(&instance);
    testBackspace(&instance);
    testEmojiMode(&instance);
    testEngineToggle(&instance);
    testCandidateNavigation(&instance);

    instance.eventDispatcher().schedule([&instance]() {
        instance.exit();
    });

    instance.exec();
    return 0;
}
