#pragma once

#include "Globals.h"
#include "FindIME.h"

#include <string>

// Read the optional process-local profile selector without a fixed-size
// buffer. Missing or empty values return an empty string.
std::wstring ReadTextServiceProfileEnvironment();
// ITfInputProcessorProfileMgr::RegisterProfile expects cchDesc in WCHARs,
// never bytes.
ULONG TextServiceDescriptionCch(const std::wstring& description);

BOOL RegisterProfiles();
void UnregisterProfiles();
BOOL RegisterCategories();
void UnregisterCategories();
BOOL RegisterServer();
BOOL UnregisterServer();

// TIP key cleanup targets (path spelling is owned by Register.cpp):
// - Registered: the real RegisterProfile location Software\Microsoft\CTF\TIP
// - LegacyTypo: bad keys written by historical installers at the misspelled
//   Software\Microsft\CTF\TIP path
enum class TipKeyPath { Registered, LegacyTypo };

// Recursively delete "{prefix}{clsid}" under root; a missing key counts as
// success. The root is injectable so tests can use a controlled hive.
BOOL DeleteTipKeyUnderRoot(HKEY root, TipKeyPath path, REFGUID clsid);
