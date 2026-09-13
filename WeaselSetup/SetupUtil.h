// WeaselSetup 的纯工具函数（命令行解析/注册表读取）。
// 不依赖 ATL/WTL，可独立编译进测试目标。

#pragma once

#include <Windows.h>
#include <string>

// 剥离命令行参数首尾成对的引号（如 /userdir:"..."，引号不属于路径本身）
std::wstring unquote_argument(const std::wstring& arg);
