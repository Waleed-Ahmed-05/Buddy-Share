#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "core/RoleManager.h"
#include "support/Fakes.h"

using buddyshare::core::Role;
using buddyshare::core::RoleManager;

namespace {

// AC1: first-ever launch (no stored role) prompts, persists, and returns the chosen role.
TEST(RoleManagerTest, NoStoredRole_ValidWriterAnswer_PersistsAndReturnsWriter) {
    FakeRoleStore store(std::nullopt);
    FakeConsole console;
    console.queue_answer("W");
    RoleManager manager(store, console);

    const Role role = manager.resolve_role();

    EXPECT_EQ(role, Role::writer);
    EXPECT_EQ(store.write_count(), 1);
    EXPECT_EQ(store.last_written(), "writer");
}

TEST(RoleManagerTest, NoStoredRole_ValidReaderAnswer_PersistsAndReturnsReader) {
    FakeRoleStore store(std::nullopt);
    FakeConsole console;
    console.queue_answer("R");
    RoleManager manager(store, console);

    const Role role = manager.resolve_role();

    EXPECT_EQ(role, Role::reader);
    EXPECT_EQ(store.write_count(), 1);
    EXPECT_EQ(store.last_written(), "reader");
}

TEST(RoleManagerTest, NoStoredRole_LowercaseAnswerAccepted) {
    FakeRoleStore store(std::nullopt);
    FakeConsole console;
    console.queue_answer("w");
    RoleManager manager(store, console);

    EXPECT_EQ(manager.resolve_role(), Role::writer);
}

// Edge case: garbage input must re-prompt, never silently default to a role.
TEST(RoleManagerTest, NoStoredRole_GarbageInput_RepromptsRatherThanDefaulting) {
    FakeRoleStore store(std::nullopt);
    FakeConsole console;
    console.queue_answer("banana");
    console.queue_answer("W");
    RoleManager manager(store, console);

    const Role role = manager.resolve_role();

    EXPECT_EQ(role, Role::writer);
    EXPECT_EQ(console.prompt_call_count(), 2);
}

// Edge case: blank input is also invalid input, not a default.
TEST(RoleManagerTest, NoStoredRole_BlankInput_Reprompts) {
    FakeRoleStore store(std::nullopt);
    FakeConsole console;
    console.queue_answer("");
    console.queue_answer("R");
    RoleManager manager(store, console);

    const Role role = manager.resolve_role();

    EXPECT_EQ(role, Role::reader);
    EXPECT_EQ(console.prompt_call_count(), 2);
}

// AC1: valid stored role skips the prompt entirely on every subsequent launch.
TEST(RoleManagerTest, StoredValidWriterRole_SkipsPromptEntirely) {
    FakeRoleStore store(std::make_optional<std::string>("writer"));
    FakeConsole console;
    RoleManager manager(store, console);

    const Role role = manager.resolve_role();

    EXPECT_EQ(role, Role::writer);
    EXPECT_EQ(console.prompt_call_count(), 0);
    EXPECT_EQ(store.write_count(), 0);
}

TEST(RoleManagerTest, StoredValidReaderRole_SkipsPromptEntirely) {
    FakeRoleStore store(std::make_optional<std::string>("reader"));
    FakeConsole console;
    RoleManager manager(store, console);

    const Role role = manager.resolve_role();

    EXPECT_EQ(role, Role::reader);
    EXPECT_EQ(console.prompt_call_count(), 0);
}

// Edge case: a corrupted/unparseable .role file is treated the same as a missing one --
// re-prompt and rewrite it, don't crash and don't silently pick a role.
TEST(RoleManagerTest, CorruptedStoredRole_TreatedAsMissing_RepromptsAndRewrites) {
    FakeRoleStore store(std::make_optional<std::string>("banana"));
    FakeConsole console;
    console.queue_answer("R");
    RoleManager manager(store, console);

    const Role role = manager.resolve_role();

    EXPECT_EQ(role, Role::reader);
    EXPECT_EQ(console.prompt_call_count(), 1);
    EXPECT_EQ(store.write_count(), 1);
    EXPECT_EQ(store.last_written(), "reader");
}

}  // namespace
