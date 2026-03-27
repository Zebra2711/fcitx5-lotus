/*
 * SPDX-FileCopyrightText: 2026 fcitx5-lotus contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Unit tests for CGoObject RAII wrapper.
// Verifies move semantics, reset, release, and bool conversion
// without requiring a live bamboo engine — a stub DeleteObject is used.

#include "fcitx-utils/log.h"
#include <optional>

// ---------------------------------------------------------------------------
// Stub: track DeleteObject calls without linking bamboo-core
// ---------------------------------------------------------------------------
static int g_delete_count = 0;
extern "C" void DeleteObject(uintptr_t /*handle*/) {
    ++g_delete_count;
}

#include "lotus.h"

using fcitx::CGoObject;

static void test_default_ctor() {
    CGoObject obj;
    FCITX_ASSERT(!obj);
    FCITX_ASSERT(obj.handle() == 0);
}

static void test_handle_ctor() {
    g_delete_count = 0;
    {
        CGoObject obj(uintptr_t{42});
        FCITX_ASSERT(obj);
        FCITX_ASSERT(obj.handle() == 42);
    }
    FCITX_ASSERT(g_delete_count == 1); // destroyed on scope exit
}

static void test_move_ctor() {
    g_delete_count = 0;
    CGoObject a(uintptr_t{10});
    CGoObject b(std::move(a));
    FCITX_ASSERT(!a);              // source emptied
    FCITX_ASSERT(b);
    FCITX_ASSERT(b.handle() == 10);
    FCITX_ASSERT(g_delete_count == 0); // no delete yet
}  // b destroyed here → 1 delete

static void test_move_assign() {
    g_delete_count = 0;
    CGoObject a(uintptr_t{1});
    CGoObject b(uintptr_t{2});
    b = std::move(a);
    FCITX_ASSERT(g_delete_count == 1); // old handle 2 released on assign
    FCITX_ASSERT(!a);
    FCITX_ASSERT(b.handle() == 1);
}

static void test_reset() {
    g_delete_count = 0;
    CGoObject obj(uintptr_t{5});
    obj.reset(uintptr_t{9});
    FCITX_ASSERT(g_delete_count == 1); // old handle released
    FCITX_ASSERT(obj.handle() == 9);

    obj.reset();
    FCITX_ASSERT(g_delete_count == 2);
    FCITX_ASSERT(!obj);
}

static void test_release() {
    g_delete_count = 0;
    CGoObject obj(uintptr_t{7});
    uintptr_t v = obj.release();
    FCITX_ASSERT(v == 7);
    FCITX_ASSERT(!obj);
    FCITX_ASSERT(g_delete_count == 0); // ownership transferred, no delete
}

static void test_self_move_assign() {
    g_delete_count = 0;
    CGoObject obj(uintptr_t{3});
    // Self-move must not double-free
    obj = std::move(obj);
    // Behaviour after self-move is implementation-defined but must not crash
    (void)obj;
}

int main() {
    test_default_ctor();
    test_handle_ctor();
    test_move_ctor();
    test_move_assign();
    test_reset();
    test_release();
    test_self_move_assign();
    return 0;
}
