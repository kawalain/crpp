#include "tray.hpp"

#include <shellapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cwchar>
#include <string_view>

#include "crpp/ui/com.hpp"
#include "os_policy.hpp"

namespace crpp::gui {

namespace {

// The settings droplet's outline (settings_panel.cpp), in a 36x48 box.
constexpr std::string_view droplet_path = "M18 1C13 10 2 20 2 31a16 16 0 0 0 32 0C34 20 23 10 18 1Z";

NOTIFYICONDATAW base_data(HWND hwnd) {
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof data;
  data.hWnd = hwnd;
  data.uID = tray_icon_id;
  return data;
}

template <std::size_t N>
void copy_text(wchar_t (&dst)[N], std::wstring_view text) {
  const std::size_t n = os_policy::fit_utf16(text, N);
  std::wmemcpy(dst, text.data(), n);
  dst[n] = L'\0';
}

}  // namespace

std::vector<std::uint32_t> render_pixels(ID2D1Factory* factory, int size,
                                         const std::function<void(ID2D1RenderTarget*)>& draw) {
  if (size <= 0) return {};
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  std::vector<std::uint32_t> pixels;
  {
    ui::ComPtr<IWICImagingFactory> wic;
    ui::ComPtr<IWICBitmap> bitmap;
    ui::ComPtr<ID2D1RenderTarget> rt;
    const auto side = static_cast<UINT>(size);
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                   reinterpret_cast<void**>(wic.put()))) &&
        SUCCEEDED(wic->CreateBitmap(side, side, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.put())) &&
        SUCCEEDED(factory->CreateWicBitmapRenderTarget(bitmap.get(), props, rt.put()))) {
      rt->BeginDraw();
      rt->Clear(D2D1::ColorF(0, 0, 0, 0));
      draw(rt.get());
      if (SUCCEEDED(rt->EndDraw())) {
        pixels.resize(side * side);
        if (FAILED(bitmap->CopyPixels(nullptr, side * 4, side * side * 4, reinterpret_cast<BYTE*>(pixels.data())))) {
          pixels.clear();
        }
      }
    }
  }
  if (SUCCEEDED(com)) CoUninitialize();
  // Icons take straight alpha; Direct2D draws premultiplied.
  for (auto& p : pixels) {
    const std::uint32_t a = p >> 24;
    if (a == 0 || a == 255) continue;
    const auto straight = [&](int shift) { return std::min<std::uint32_t>(((p >> shift) & 0xFF) * 255 / a, 255) << shift; };
    p = (a << 24) | straight(16) | straight(8) | straight(0);
  }
  return pixels;
}

std::vector<std::uint32_t> render_droplet(const ui::Gfx& gfx, int size) {
  const auto shape = gfx.parse_path(droplet_path);
  if (!shape) return {};
  return render_pixels(gfx.d2d(), size, [&](ID2D1RenderTarget* rt) {
    const float s = static_cast<float>(size);
    const float scale = s / 48;
    rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation((s - 36 * scale) / 2, 0));
    ui::ComPtr<ID2D1SolidColorBrush> brush;
    // Readable on the taskbar's dark and light themes alike.
    if (SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(0x5DADEC), brush.put()))) rt->FillGeometry(shape.get(), brush.get());
  });
}

HICON icon_from_pixels(const std::vector<std::uint32_t>& pixels, int size) {
  if (size <= 0 || pixels.size() != static_cast<std::size_t>(size) * static_cast<std::size_t>(size)) return nullptr;
  BITMAPV5HEADER header{};
  header.bV5Size = sizeof header;
  header.bV5Width = size;
  header.bV5Height = -size;  // top row first
  header.bV5Planes = 1;
  header.bV5BitCount = 32;
  header.bV5Compression = BI_BITFIELDS;
  header.bV5RedMask = 0x00FF0000;
  header.bV5GreenMask = 0x0000FF00;
  header.bV5BlueMask = 0x000000FF;
  header.bV5AlphaMask = 0xFF000000;
  void* bits = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
  HICON icon = nullptr;
  if (color && mask && bits) {
    std::copy(pixels.begin(), pixels.end(), static_cast<std::uint32_t*>(bits));
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = color;
    icon = CreateIconIndirect(&info);  // copies both bitmaps
  }
  if (color) DeleteObject(color);
  if (mask) DeleteObject(mask);
  return icon;
}

bool TrayIcon::add() {
  if (present_) return true;
  const int size = GetSystemMetrics(SM_CXSMICON);
  HICON icon = icon_from_pixels(render_droplet(*gfx_, size), size);
  if (!icon) return false;
  auto data = base_data(hwnd_);
  data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  data.uCallbackMessage = wm_tray;
  data.hIcon = icon;
  copy_text(data.szTip, L"Cloudroom++");
  present_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
  DestroyIcon(icon);
  if (!present_) return false;
  // Must follow every add. Version 4 reports events in LOWORD(lParam).
  data.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &data);
  return true;
}

void TrayIcon::remove() {
  if (!present_) return;
  auto data = base_data(hwnd_);
  Shell_NotifyIconW(NIM_DELETE, &data);
  present_ = false;
}

bool TrayIcon::notify(std::wstring_view title, std::wstring_view body, HICON balloon_icon, DWORD flags) {
  if (!present_) return false;
  auto data = base_data(hwnd_);
  data.uFlags = NIF_INFO | NIF_SHOWTIP;
  copy_text(data.szInfoTitle, title);
  copy_text(data.szInfo, body);
  data.dwInfoFlags = flags;
  if (balloon_icon) {
    data.hBalloonIcon = balloon_icon;
    data.dwInfoFlags |= NIIF_USER | NIIF_LARGE_ICON;
  }
  return Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
}

void TrayIcon::clear_notification() {
  if (!present_) return;
  auto data = base_data(hwnd_);
  data.uFlags = NIF_INFO | NIF_SHOWTIP;
  Shell_NotifyIconW(NIM_MODIFY, &data);
}

}  // namespace crpp::gui
