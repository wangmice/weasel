#include "stdafx.h"
#include "Deserializer.h"
#include "ActionLoader.h"
#include "Committer.h"
#include "ContextUpdater.h"
#include "Configurator.h"
#include "Styler.h"

using namespace weasel;

namespace {
// Action registry shared by every ResponseParser. Initialized on first use:
// C++11 magic statics make that thread-safe, so several UI threads parsing
// their first response concurrently are safe (the former empty()-check plus
// insert was an unsynchronized race).
const std::map<std::wstring, Deserializer::Factory>& GetFactories() {
  static const std::map<std::wstring, Deserializer::Factory> factories = {
      // TODO: extend the parser's functionality in the future by defining
      // more actions here
      {L"action", ActionLoader::Create},
      {L"commit", Committer::Create},
      {L"ctx", ContextUpdater::Create},
      {L"status", StatusUpdater::Create},
      {L"config", Configurator::Create},
      {L"style", Styler::Create}};
  return factories;
}
}  // namespace

void Deserializer::Initialize(ResponseParser* pTarget) {
  // loaded by default
  Require(L"action", pTarget);
}

bool Deserializer::Require(std::wstring const& action,
                           ResponseParser* pTarget) {
  if (!pTarget)
    return false;

  auto const& factories = GetFactories();
  auto i = factories.find(action);
  if (i == factories.end()) {
    // unknown action type
    return false;
  }

  Factory const& factory = i->second;
  pTarget->deserializers[action] = factory(pTarget);
  return true;
}
