/* Global definitions */
#pragma once

#include "stdafx.h"
#include <WeaselConstants.h>
#include <functional>

#define TEXTSERVICE_LANGID_HANT \
  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)
#define TEXTSERVICE_LANGID_HONGKONG \
  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_HONGKONG)
#define TEXTSERVICE_LANGID_MACAU MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_MACAU)
#define TEXTSERVICE_LANGID_SINGAPORE \
  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SINGAPORE)
#define TEXTSERVICE_LANGID_HANS \
  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)

#define TEXTSERVICE_DESC_A WEASEL_CODE_NAME
#define TEXTSERVICE_MODEL "Apartment"

#define TEXTSERVICE_ICON_INDEX 0

void DllAddRef();
void DllRelease();

// Submit work without blocking the TSF UI thread while keeping both COM's
// logical unload count and the Windows loader reference alive until the
// callback has completely returned.
bool QueueDllBackgroundTask(std::function<void()> task);

extern HINSTANCE g_hInst;

extern LONG g_cRefDll;

extern CRITICAL_SECTION g_cs;

extern const CLSID c_clsidTextService;

extern const GUID c_guidProfile;

extern const GUID c_guidLangBarItemButton;

extern const GUID c_guidDisplayAttributeInput;

#ifndef TF_IPP_CAPS_IMMERSIVESUPPORT

#define WEASEL_USING_OLDER_TSF_SDK

/* For Windows 8 */
#define TF_TMF_IMMERSIVEMODE 0x40000000
#define TF_IPP_CAPS_IMMERSIVESUPPORT 0x00010000
#define TF_IPP_CAPS_SYSTRAYSUPPORT 0x00020000

extern const GUID GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT;
extern const GUID GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT;

#endif

extern const GUID GUID_LBI_INPUTMODE;
extern const GUID GUID_IME_MODE_PRESERVED_KEY;
