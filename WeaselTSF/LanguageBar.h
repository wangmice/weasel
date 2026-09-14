#pragma once
#include <msctf.h>
#include <WeaselUI.h>

class CLangBarItemButton : public ITfLangBarItemButton, public ITfSource {
 public:
  CLangBarItemButton(WeaselTSF* pTextService,
                     REFGUID guid,
                     weasel::UIStyle& style);
  ~CLangBarItemButton();

  /* IUnknown */
  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  /* ITfLangBarItem */
  STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* pInfo);
  STDMETHODIMP GetStatus(DWORD* pdwStatus);
  STDMETHODIMP Show(BOOL fShow);
  STDMETHODIMP GetTooltipString(BSTR* pbstrToolTip);

  /* ITfLangBarItemButton */
  STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT* prcArea);
  STDMETHODIMP InitMenu(ITfMenu* pMenu);
  STDMETHODIMP OnMenuSelect(UINT wID);
  STDMETHODIMP GetIcon(HICON* phIcon);
  STDMETHODIMP GetText(BSTR* pbstrText);

  /* ITfSource */
  STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie);
  STDMETHODIMP UnadviseSink(DWORD dwCookie);
  BOOL IsLangBarDisabled() { return (_status & TF_LBI_STATUS_DISABLED); }

  void UpdateWeaselStatus(weasel::Status stat);
  void SetLangbarStatus(DWORD dwStatus, BOOL fSet);

 private:
  // 自定义图标文件缓存：master 句柄归本对象所有，GetIcon 每次返回
  // CopyIcon 副本（语言栏按 MSDN 约定销毁返回值）；路径变化即重载
  HICON _LoadCachedFileIcon(const std::wstring& path,
                            std::wstring& cachedPath,
                            HICON& cachedIcon);
  void _DestroyCachedIcons();

  GUID _guid;
  // Non-owning back pointer: its lifetime is strictly nested in WeaselTSF;
  // holding a COM reference here would create a reference cycle.
  WeaselTSF* _pTextService;
  com_ptr<ITfLangBarItemSink> _pLangBarItemSink;
  LONG _cRef; /* COM Reference count */
  DWORD _status;
  bool ascii_mode;
  weasel::UIStyle& _style;
  std::wstring _current_schema_zhung_icon;
  std::wstring _current_schema_ascii_icon;
  std::wstring _zhung_icon_path;
  HICON _zhung_icon = NULL;
  std::wstring _ascii_icon_path;
  HICON _ascii_icon = NULL;
};
