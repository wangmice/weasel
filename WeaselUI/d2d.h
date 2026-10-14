#pragma once
#ifndef D2D_H
#define D2D_H

#include <atltypes.h>
#include <WeaselIPCData.h>
#include <cstdint>
#include <d2d1.h>
#include <d2d1_2.h>
#include <d2d1_2helper.h>
#include <d3d11_2.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dwrite_2.h>
#include <dxgi1_3.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <wincodec.h>
#include <WeaselUtility.h>

namespace weasel {

typedef ComPtr<IDWriteTextFormat1> PtTextFormat;

constexpr D2D1_DEBUG_LEVEL SelectD2DDebugLevel(
    bool enable_debug_layer) noexcept {
  return enable_debug_layer ? D2D1_DEBUG_LEVEL_INFORMATION
                            : D2D1_DEBUG_LEVEL_NONE;
}

template <typename T>
void SafeRelease(ComPtr<T>& t) {
  if (t) {
    t.Reset();
    t = nullptr;
  }
}

template <typename... Ts>
void SafeReleaseAll(Ts&&... args) {
  (SafeRelease(std::forward<Ts>(args)), ...);
}

template <typename T>
T MAX(T a) {
  return a;
}
template <typename T, typename... Args>
T MAX(T a, Args... args) {
  T max_rest = MAX(args...);
  return a > max_rest ? a : max_rest;
}
template <typename T>
T MIN(T a) {
  return a;
}
template <typename T, typename... Args>
T MIN(T a, Args... args) {
  T min_rest = MIN(args...);
  return a < min_rest ? a : min_rest;
}

// Immutable process-shared graphics generation. D2D instances keep a shared
// owner so a device-loss rollover can publish a fresh generation without
// invalidating resources that another UI thread is still unwinding.
//
// DirectComposition devices stay per D2D instance so commits from independent
// TSF threads remain separate transactions. WIC factories also stay local to
// the owning UI/COM context instead of being passed directly across apartments.
struct DeviceResourcesSnapshot {
  uint64_t generation = 0;
  ComPtr<ID3D11Device> direct3dDevice;
  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIFactory2> dxFactory;
  ComPtr<ID2D1Factory2> d2Factory;
  ComPtr<ID2D1Device1> d2Device;
  ComPtr<IDWriteFactory2> m_pWriteFactory;
};

// Publishes complete shared generations under a mutex, but owns them only
// weakly. The final D2D owner therefore releases COM resources in its normal
// destruction path instead of from a DLL/static destructor under loader lock.
class DeviceResources {
 public:
  static DeviceResources& Get();

  HRESULT Acquire(std::shared_ptr<const DeviceResourcesSnapshot>& snapshot);
  HRESULT RecreateAfterDeviceLoss(
      uint64_t failed_generation,
      std::shared_ptr<const DeviceResourcesSnapshot>& snapshot);
  bool HasLiveGeneration() const;

 private:
  DeviceResources() = default;
  HRESULT CreateGenerationLocked(
      std::shared_ptr<const DeviceResourcesSnapshot>& snapshot);

  mutable std::mutex mutex_;
  std::weak_ptr<const DeviceResourcesSnapshot> current_;
  uint64_t next_generation_ = 1;
};

struct IsToRoundStruct {
  bool IsTopLeftNeedToRound;
  bool IsBottomLeftNeedToRound;
  bool IsTopRightNeedToRound;
  bool IsBottomRightNeedToRound;
  bool Hemispherical;
  IsToRoundStruct()
      : IsTopLeftNeedToRound(true),
        IsTopRightNeedToRound(true),
        IsBottomLeftNeedToRound(true),
        IsBottomRightNeedToRound(true),
        Hemispherical(false) {}
};

struct D2D {
  // Construct without window; call AttachWindow when HWND is ready.
  D2D(UIStyle& style);
  // Allow constructing before window exists; attach window later to finish
  // init.
  void AttachWindow(HWND hwnd);
  ~D2D();
  void InitDirect2D();
  void InitDirectWriteResources();
  void InitDpiInfo();
  void InitFontFormats();
  PtTextFormat GetOrCreateTextFormat(const std::wstring& face,
                                     int point,
                                     DWRITE_WORD_WRAPPING wrap);
  void InitFontFormats(const wstring& label_font_face,
                       const int label_font_point,
                       const wstring& font_face,
                       const int font_point,
                       const wstring& comment_font_face,
                       const int comment_font_point);
  void OnResize(UINT width, UINT height);
  void SetBrushColor(uint32_t color);
  void GetTextSize(const wstring& text,
                   size_t nCount,
                   PtTextFormat& pTextFormat,
                   LPSIZE lpSize);
  void SetFontFallback(PtTextFormat textFormat,
                       const std::vector<std::wstring>& fontVector);
  void ParseFontFace(const std::wstring& fontFaceStr,
                     DWRITE_FONT_WEIGHT& fontWeight,
                     DWRITE_FONT_STYLE& fontStyle,
                     DWRITE_FONT_STRETCH& fontStretch);
  HRESULT GetBmpFromIcon(HICON hIcon, ComPtr<ID2D1Bitmap1>& pBitmap);
  HRESULT GetCachedBmpFromIcon(HICON hIcon, ComPtr<ID2D1Bitmap1>& pBitmap);

  HRESULT CreateRoundedRectanglePath(const RECT& rc,
                                     float radius,
                                     const IsToRoundStruct& roundInfo,
                                     ComPtr<ID2D1PathGeometry>& pPathGeometry);
  HRESULT FillGeometry(const CRect& rect,
                       uint32_t color,
                       uint32_t radius,
                       IsToRoundStruct roundInfo,
                       bool to_blur = false);
  HRESULT DrawTextLayout(ComPtr<IDWriteTextLayout> pTextLayout,
                         float x,
                         float y,
                         uint32_t color);
  ComPtr<ID3D11Device> direct3dDevice;
  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIFactory2> dxFactory;
  ComPtr<IDXGISwapChain1> swapChain;
  ComPtr<ID2D1Factory2> d2Factory;
  ComPtr<ID2D1Device1> d2Device;
  ComPtr<ID2D1DeviceContext> dc;
  ComPtr<IDXGISurface2> surface;
  ComPtr<ID2D1Bitmap1> bitmap;
  ComPtr<IDCompositionDevice> dcompDevice;
  ComPtr<IDCompositionTarget> target;
  ComPtr<IDCompositionVisual> visual;
  PtTextFormat pPreeditFormat;
  PtTextFormat pTextFormat;
  PtTextFormat pLabelFormat;
  PtTextFormat pCommentFormat;
  ComPtr<IDWriteFactory2> m_pWriteFactory;
  ComPtr<IWICImagingFactory> wicFactory;
  ComPtr<ID2D1SolidColorBrush> m_pBrush;
  HICON cachedIcon = nullptr;
  ComPtr<ID2D1Bitmap1> cachedIconBitmap;
  struct CachedTextFormat {
    PtTextFormat format;
    uint64_t last_used;
  };

  static constexpr size_t kMaxTextFormatCacheEntries = 32;
  std::map<std::wstring, CachedTextFormat> textFormatCache;
  uint64_t textFormatCacheClock = 0;
  std::mutex cacheMutex;
  // clear caches that depend on device/context
  void ClearDeviceDependentCaches();
  // Release per-window resources without invalidating shared devices.
  void ReleaseWindowResources();
  UIStyle& m_style;
  HWND m_hWnd;
  float m_dpiX;
  float m_dpiY;
  float m_dpiScaleFontPoint;
  float m_dpiScaleLayout;

 private:
  HRESULT AcquireSharedResources();
  void AdoptSharedResources(
      const std::shared_ptr<const DeviceResourcesSnapshot>& snapshot);
  std::shared_ptr<const DeviceResourcesSnapshot> m_sharedResources;
};
}  // namespace weasel
#endif
