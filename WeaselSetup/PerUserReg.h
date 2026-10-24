#pragma once

#include <Windows.h>
#include <string>

// Per-user hive redirection. When the setup elevates for another user,
// HKCU inside the elevated process maps to the admin's hive. The invoking
// user's SID, captured before elevation, redirects per-user accesses to
// HKEY_USERS\<sid> so values land in (and come from) the real user's hive.

std::wstring current_user_sid();

// Parses and strips the trailing "/origsid:<S-...>" parameter appended by
// RestartAsAdmin; arms the redirection when a well-formed SID is present.
void apply_orig_sid_param(LPTSTR lpCmdLine);

// Appends "/origsid:<current user>" to the command line before relaunching
// elevated, so the elevated instance can address this user's hive.
std::wstring with_orig_sid_param(const std::wstring& lpCmdLine);

void set_per_user_origin_sid(const std::wstring& sid);
bool per_user_redirected();
HKEY per_user_root();  // HKEY_USERS when redirected, else HKEY_CURRENT_USER
std::wstring per_user_subkey(const wchar_t* sub);
std::wstring per_user_default_dir();  // invoking user's %APPDATA%\Rime
