#pragma once

#include "Globals.h"
#include "FindIME.h"

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
