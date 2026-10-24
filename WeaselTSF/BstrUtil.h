#pragma once
#include <Windows.h>
#include <oleauto.h>
#include <string>

// 分配内容与 s 完全一致的 BSTR。
// SysAllocStringLen(p, len + 1) 会把末尾 NUL 计入长度，
// SysStringLen 将比源串多 1（K4）。
inline BSTR AllocBstr(const std::wstring& s) {
  return SysAllocStringLen(s.c_str(), static_cast<UINT>(s.size()));
}
