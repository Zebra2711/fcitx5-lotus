/*
 * SPDX-FileCopyrightText: 2026 fcitx5-lotus contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Unit tests for lotus-utils pure functions:
//   compareAndSplitStrings, isBackspace, isStartsWith

#include "fcitx-utils/log.h"
#include "fcitx-utils/keysym.h"
#include "lotus-utils.h"

static void test_isStartsWith() {
    FCITX_ASSERT(isStartsWith("hello", "hel"));
    FCITX_ASSERT(isStartsWith("hello", "hello"));
    FCITX_ASSERT(isStartsWith("hello", ""));
    FCITX_ASSERT(!isStartsWith("hello", "world"));
    FCITX_ASSERT(!isStartsWith("", "x"));
    FCITX_ASSERT(!isStartsWith("ab", "abc"));
}

static void test_isBackspace() {
    FCITX_ASSERT(isBackspace(FcitxKey_BackSpace));
    FCITX_ASSERT(!isBackspace(FcitxKey_Delete));
    FCITX_ASSERT(!isBackspace(FcitxKey_a));
    FCITX_ASSERT(!isBackspace(0));
}

static void test_compareAndSplitStrings() {
    std::string prefix, deleted, added;

    // Identical strings
    int r = compareAndSplitStrings("abc", "abc", prefix, deleted, added);
    (void)r;
    FCITX_ASSERT(prefix == "abc");
    FCITX_ASSERT(deleted.empty());
    FCITX_ASSERT(added.empty());

    // Pure addition
    compareAndSplitStrings("ab", "abcd", prefix, deleted, added);
    FCITX_ASSERT(prefix == "ab");
    FCITX_ASSERT(deleted.empty());
    FCITX_ASSERT(added == "cd");

    // Pure deletion
    compareAndSplitStrings("abcd", "ab", prefix, deleted, added);
    FCITX_ASSERT(prefix == "ab");
    FCITX_ASSERT(deleted == "cd");
    FCITX_ASSERT(added.empty());

    // Replacement: "xây" → "xây dựng" (Vietnamese multibyte)
    compareAndSplitStrings("xây", "xây dựng", prefix, deleted, added);
    FCITX_ASSERT(prefix == "xây");
    FCITX_ASSERT(deleted.empty());
    FCITX_ASSERT(added == " dựng");

    // Telex correction: "banh" → "bánh"
    compareAndSplitStrings("banh", "bánh", prefix, deleted, added);
    FCITX_ASSERT(!added.empty());  // engine added the accented version

    // Both strings empty
    compareAndSplitStrings("", "", prefix, deleted, added);
    FCITX_ASSERT(prefix.empty());
    FCITX_ASSERT(deleted.empty());
    FCITX_ASSERT(added.empty());

    // A empty, B non-empty
    compareAndSplitStrings("", "abc", prefix, deleted, added);
    FCITX_ASSERT(prefix.empty());
    FCITX_ASSERT(deleted.empty());
    FCITX_ASSERT(added == "abc");

    // A non-empty, B empty
    compareAndSplitStrings("abc", "", prefix, deleted, added);
    FCITX_ASSERT(prefix.empty());
    FCITX_ASSERT(deleted == "abc");
    FCITX_ASSERT(added.empty());
}

int main() {
    test_isStartsWith();
    test_isBackspace();
    test_compareAndSplitStrings();
    return 0;
}
