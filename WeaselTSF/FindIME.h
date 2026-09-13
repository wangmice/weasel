#pragma once

#include <Windows.h>

HKL FindIME(LANGID langid);

// Scans the E02x..E0Fx `<langid>`-suffixed subkeys under `root` for a layout
// whose "Ime File" matches `imeFile`. Split out with an injectable root key
// so tests can drive it against HKEY_CURRENT_USER without elevation.
HKL FindIMEUnderKey(HKEY root, LANGID langid, const wchar_t* imeFile);
