#include <Windows.h>
#include <strsafe.h>

#include "FindIME.h"

HKL FindIMEUnderKey(HKEY root, LANGID langid, const wchar_t* imeFile) {
  HKL hKL = NULL;
  WCHAR key[9];
  for (DWORD id = (0xE0200000 | langid);
       hKL == NULL && id <= (0xE0FF0000 | langid); id += 0x10000) {
    StringCchPrintfW(key, _countof(key), L"%08X", id);
    HKEY hSubKey = NULL;
    LSTATUS ret = RegOpenKeyExW(root, key, 0, KEY_READ, &hSubKey);
    if (ret == ERROR_SUCCESS) {
      WCHAR data[32] = {0};
      DWORD type;
      DWORD size = sizeof data - sizeof(WCHAR);  // keep room for the NUL
      ret = RegQueryValueExW(hSubKey, L"Ime File", NULL, &type, (LPBYTE)data,
                             &size);
      if (ret == ERROR_SUCCESS && type == REG_SZ &&
          _wcsicmp(data, imeFile) == 0)
        hKL = (HKL)(DWORD_PTR)id;
    }
    if (hSubKey != NULL)
      RegCloseKey(hSubKey);
  }
  return hKL;
}

HKL FindIME(LANGID langid) {
  HKEY hKey = NULL;
  LSTATUS ret =
      RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", 0,
                    KEY_READ, &hKey);
  if (ret != ERROR_SUCCESS)
    return NULL;
  HKL hKL = FindIMEUnderKey(hKey, langid, L"weasel.ime");
  RegCloseKey(hKey);
  return hKL;
}
