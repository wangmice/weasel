// WeaselSetup 的纯工具函数（命令行解析/注册表读取）。
// 不依赖 ATL/WTL，可独立编译进测试目标。

#pragma once

#include <Windows.h>
#include <string>

// 剥离命令行参数首尾成对的引号（如 /userdir:"..."，引号不属于路径本身）
std::wstring unquote_argument(const std::wstring& arg);

// 读取 hKey 下的 REG_SZ 值。以返回的数据长度为上界查找字符串结尾，
// 存量数据未 NUL 终止时截断到缓冲区内，构造 std::wstring 不越读。
// 值超出栈上缓冲时按需扩容重查（Windows 11 24H2 起注册表可为 REG_SZ
// 追加 NUL 终止符，实际存储可大于写入长度）
bool read_reg_sz(HKEY hKey, const wchar_t* value_name, std::wstring& out);

// 递归删除 root 下 subkey 的整棵键树（含全部子键与值）。
// RegDeleteKey 遇有子键的键即失败，卸载清理必须整树删除。
// 键不存在时返回 ERROR_FILE_NOT_FOUND。
LSTATUS delete_reg_tree(HKEY root, const wchar_t* subkey);
