#include "../../WeaselIPCServer/SessionManager.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

namespace {

void require(bool condition, const char* what) {
  if (condition)
    return;
  std::cerr << "[FAIL] SessionManager: " << what << std::endl;
  std::abort();
}

weasel::SessionKey Key(std::uint8_t tail) {
  weasel::SessionKey key{};
  key.bytes.back() = tail;
  return key;
}

struct SessionManagerSelfTest {
  SessionManagerSelfTest() {
    using weasel::ClientProcessToken;
    using weasel::SessionManager;
    using weasel::SessionOwner;

    SessionManager manager;
    int creates = 0;
    std::vector<SessionManager::SessionId> destroyed;
    auto create = [&]() -> std::optional<SessionManager::SessionId> {
      ++creates;
      return static_cast<SessionManager::SessionId>(100 + creates);
    };
    auto destroy = [&](SessionManager::SessionId id) {
      destroyed.push_back(id);
    };
    auto valid = [](SessionManager::SessionId) { return true; };

    const SessionOwner owner_a{ClientProcessToken{1}};
    const SessionOwner owner_b{ClientProcessToken{2}};
    const auto key_a = Key(1);

    auto first = manager.Ensure(key_a, owner_a, valid, create, destroy);
    require(first.ipc_id == 101 && first.created && creates == 1,
            "first Ensure creates exactly once");

    auto second = manager.Ensure(key_a, owner_a, valid, create, destroy);
    require(second.ipc_id == 101 && !second.created && creates == 1,
            "same key and owner reuses the session");

    auto collision = manager.Ensure(key_a, owner_b, valid, create, destroy);
    require(collision.ipc_id == 0 && !collision.created && creates == 1 &&
                destroyed.empty(),
            "cross-process key reuse is rejected without mutation");

    bool validate_existing = false;
    auto rebuilt = manager.Ensure(
        key_a, owner_a,
        [&](SessionManager::SessionId id) {
          require(id == 101, "stale validation checks the existing id");
          return validate_existing;
        },
        create, destroy);
    require(rebuilt.ipc_id == 102 && rebuilt.created && creates == 2 &&
                destroyed.size() == 1 && destroyed[0] == 101,
            "stale keyed session is destroyed and rebuilt once");
    require(manager.Find(key_a).has_value() && *manager.Find(key_a) == 102,
            "rebuilt key points only at the replacement");

    SessionManager unowned;
    int unowned_creates = 0;
    auto unowned_create = [&]() -> std::optional<SessionManager::SessionId> {
      ++unowned_creates;
      return 201;
    };
    auto unowned_first =
        unowned.Ensure(Key(2), {}, valid, unowned_create, destroy);
    auto unowned_bound =
        unowned.Ensure(Key(2), owner_a, valid, unowned_create, destroy);
    require(unowned_first.ipc_id == 201 && unowned_bound.ipc_id == 201 &&
                !unowned_bound.created && unowned_creates == 1,
            "later authoritative owner binds an initially unowned key");

    require(manager.Remove(102, destroy), "Remove reports first removal");
    require(!manager.Remove(102, destroy), "Remove is idempotent");
    require(!manager.Find(key_a).has_value(), "Remove erases key index");

    SessionManager owned;
    require(owned.BindLegacy(301, owner_a), "legacy session binds to owner A");
    require(owned.BindLegacy(302, owner_a),
            "second legacy session binds to owner A");
    require(owned.BindLegacy(401, owner_b), "legacy session binds to owner B");
    require(!owned.Find(Key(9)).has_value(),
            "legacy sessions have no key lookup");

    std::vector<SessionManager::SessionId> owner_destroyed;
    owned.RemoveClient(
        owner_a.process.value(),
        [&](SessionManager::SessionId id) { owner_destroyed.push_back(id); });
    require(owner_destroyed.size() == 2,
            "RemoveClient removes every session owned by one process");
    require(owned.Remove(401, destroy),
            "RemoveClient preserves sessions owned by another process");

    SessionManager cleared;
    require(cleared.Ensure(
                       Key(3), owner_a, valid,
                       []() -> std::optional<SessionManager::SessionId> {
                         return 501;
                       },
                       destroy)
                    .ipc_id == 501,
            "Clear setup keyed session created");
    require(cleared.BindLegacy(502, owner_b),
            "Clear setup legacy session bound");
    int clear_destroyed = 0;
    cleared.Clear([&](SessionManager::SessionId) { ++clear_destroyed; });
    require(clear_destroyed == 2 && !cleared.Find(Key(3)).has_value(),
            "Clear destroys all sessions and empties key index");
  }
};

SessionManagerSelfTest g_session_manager_self_test;

}  // namespace
