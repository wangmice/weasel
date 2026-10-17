#pragma once

#include <WeaselSessionLifecycle.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace weasel {

// Logical-session identity/lifetime indexes. The owner (ServerImpl via
// SessionRoutingHandler) serializes all calls under g_api_mutex, so this class
// intentionally carries no internal mutex and never participates in lock order.
class SessionManager {
 public:
  using SessionId = std::uint32_t;

  struct EnsureResult {
    SessionId ipc_id = 0;
    bool created = false;
  };

  using Create = std::function<std::optional<SessionId>()>;
  using Destroy = std::function<void(SessionId)>;
  using Validate = std::function<bool(SessionId)>;

  EnsureResult Ensure(const SessionKey& key,
                      SessionOwner owner,
                      Validate validate,
                      Create create,
                      Destroy destroy) {
    SessionOwner effective_owner = owner;

    const auto keyed = keyed_.find(key);
    if (keyed != keyed_.end()) {
      const auto session = sessions_.find(keyed->second);
      if (session == sessions_.end()) {
        keyed_.erase(keyed);
      } else {
        Entry& entry = session->second;
        if (entry.owner.process && owner.process &&
            *entry.owner.process != *owner.process) {
          return {};
        }

        if (!entry.owner.process && owner.process)
          entry.owner = owner;
        if (!effective_owner.process)
          effective_owner = entry.owner;

        if (!validate || validate(entry.ipc_id))
          return {entry.ipc_id, false};

        const SessionId stale_id = entry.ipc_id;
        const Entry stale_entry = entry;
        EraseIndexes(stale_entry);
        if (destroy)
          destroy(stale_id);
      }
    }

    if (!create)
      return {};
    const std::optional<SessionId> created = create();
    if (!created || *created == 0)
      return {};
    if (sessions_.find(*created) != sessions_.end()) {
      if (destroy)
        destroy(*created);
      return {};
    }

    Entry entry;
    entry.ipc_id = *created;
    entry.key = key;
    entry.owner = effective_owner;
    sessions_.emplace(entry.ipc_id, entry);
    keyed_.emplace(key, entry.ipc_id);
    return {entry.ipc_id, true};
  }

  bool BindLegacy(SessionId ipc_id, SessionOwner owner) {
    if (ipc_id == 0 || sessions_.find(ipc_id) != sessions_.end())
      return false;

    Entry entry;
    entry.ipc_id = ipc_id;
    entry.owner = owner;
    sessions_.emplace(ipc_id, std::move(entry));
    return true;
  }

  bool Remove(SessionId ipc_id, Destroy destroy) {
    const auto session = sessions_.find(ipc_id);
    if (session == sessions_.end())
      return false;

    const Entry entry = session->second;
    EraseIndexes(entry);
    if (destroy)
      destroy(ipc_id);
    return true;
  }

  void RemoveClient(ClientProcessToken owner, Destroy destroy) {
    std::vector<SessionId> ids;
    for (const auto& [id, entry] : sessions_) {
      if (entry.owner.process && *entry.owner.process == owner)
        ids.push_back(id);
    }
    for (SessionId id : ids)
      Remove(id, destroy);
  }

  void Clear(Destroy destroy) {
    std::vector<SessionId> ids;
    ids.reserve(sessions_.size());
    for (const auto& [id, entry] : sessions_) {
      (void)entry;
      ids.push_back(id);
    }

    sessions_.clear();
    keyed_.clear();
    if (destroy) {
      for (SessionId id : ids)
        destroy(id);
    }
  }

  std::optional<SessionId> Find(const SessionKey& key) const {
    const auto it = keyed_.find(key);
    if (it == keyed_.end())
      return std::nullopt;
    return it->second;
  }

 private:
  struct Entry {
    SessionId ipc_id = 0;
    std::optional<SessionKey> key;
    SessionOwner owner;
  };

  void EraseIndexes(const Entry& entry) {
    if (entry.key) {
      const auto keyed = keyed_.find(*entry.key);
      if (keyed != keyed_.end() && keyed->second == entry.ipc_id)
        keyed_.erase(keyed);
    }
    sessions_.erase(entry.ipc_id);
  }

  std::map<SessionKey, SessionId> keyed_;
  std::map<SessionId, Entry> sessions_;
};

}  // namespace weasel
