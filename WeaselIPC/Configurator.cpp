#include "stdafx.h"
#include "Deserializer.h"
#include "Configurator.h"

using namespace weasel;

Deserializer::Ptr Configurator::Create(ResponseParser* pTarget) {
  return Deserializer::Ptr(new Configurator(pTarget));
}

Configurator::Configurator(ResponseParser* pTarget) : Deserializer(pTarget) {}

Configurator::~Configurator() {}

void Configurator::Store(Deserializer::KeyType const& key,
                         std::wstring const& value) {
  // Store 只写入 p_config，守卫必须与之一致：
  // 有 context 无 config 的调用方（config 键出现在响应中）会空指针崩溃
  if (!m_pTarget->p_config || key.size() < 2)
      return;
  bool bool_value = (!value.empty() && value != L"0");
  if (key[1] == L"inline_preedit") {
    m_pTarget->p_config->inline_preedit = bool_value;
  }
}
