// crpp_make_icon <in.png> <out.ico>: scales the app's icon to the sizes Windows asks for,
// among them the 20 and 40 pixels of the tray at 125% and 150%. Run by hand when the
// picture changes; the result is committed, so a build needs no scaling step. Small sizes
// stay uncompressed, which every loader takes, and the largest is PNG, as Windows expects
// at that size.
#include <wincodec.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "crpp/ui/com.hpp"

using namespace crpp;

namespace {

using Bytes = std::vector<char>;

void put16(Bytes& out, std::uint32_t v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void put32(Bytes& out, std::uint32_t v) {
  put16(out, v & 0xFFFF);
  put16(out, v >> 16);
}

// A DIB of twice the height (colour above an AND mask, per the ICO format), bottom row first.
Bytes dib(const std::vector<std::uint32_t>& pixels, std::uint32_t size) {
  Bytes out;
  put32(out, 40);
  put32(out, size);
  put32(out, size * 2);
  put16(out, 1);
  put16(out, 32);
  for (int i = 0; i < 6; ++i) put32(out, 0);
  for (std::uint32_t y = size; y-- > 0;) {
    for (std::uint32_t x = 0; x < size; ++x) put32(out, pixels[y * size + x]);
  }
  // All zero: alpha alone decides. Mask rows are padded to 32 bits.
  out.insert(out.end(), static_cast<std::size_t>((size + 31) / 32 * 4 * size), '\0');
  return out;
}

Bytes png(const std::vector<std::uint32_t>& pixels, std::uint32_t size) {
  ui::ComPtr<IWICImagingFactory> wic;
  ui::ComPtr<IWICStream> stream;
  ui::ComPtr<IWICBitmapEncoder> encoder;
  ui::ComPtr<IWICBitmapFrameEncode> frame;
  Bytes out(size * size * 4 + 4096);
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  ULARGE_INTEGER written{};
  auto* data = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(pixels.data()));
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                              reinterpret_cast<void**>(wic.put()))) ||
      FAILED(wic->CreateStream(stream.put())) ||
      FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(out.data()), static_cast<DWORD>(out.size()))) ||
      FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) ||
      FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) ||
      FAILED(encoder->CreateNewFrame(frame.put(), nullptr)) || FAILED(frame->Initialize(nullptr)) ||
      FAILED(frame->SetSize(size, size)) || FAILED(frame->SetPixelFormat(&format)) ||
      FAILED(frame->WritePixels(size, size * 4, size * size * 4, data)) || FAILED(frame->Commit()) ||
      FAILED(encoder->Commit()) || FAILED(stream->Seek({}, STREAM_SEEK_CUR, &written))) {
    return {};
  }
  out.resize(static_cast<std::size_t>(written.QuadPart));
  return out;
}

// Straight-alpha BGRA, top row first; empty on failure. Scaled premultiplied, so what is
// under the transparent corners does not bleed into the edge.
std::vector<std::uint32_t> scaled(IWICImagingFactory* wic, IWICBitmapSource* source, std::uint32_t size) {
  ui::ComPtr<IWICBitmapScaler> scaler;
  std::vector<std::uint32_t> pixels(size * size);
  if (FAILED(wic->CreateBitmapScaler(scaler.put())) ||
      FAILED(scaler->Initialize(source, size, size, WICBitmapInterpolationModeHighQualityCubic)) ||
      FAILED(scaler->CopyPixels(nullptr, size * 4, size * size * 4, reinterpret_cast<BYTE*>(pixels.data())))) {
    return {};
  }
  for (std::uint32_t& pixel : pixels) {
    const std::uint32_t alpha = pixel >> 24;
    if (alpha == 0 || alpha == 255) continue;
    const auto straight = [&](int shift) {
      const std::uint32_t c = (pixel >> shift) & 0xFF;
      return std::min<std::uint32_t>((c * 255 + alpha / 2) / alpha, 255) << shift;
    };
    pixel = (alpha << 24) | straight(16) | straight(8) | straight(0);
  }
  return pixels;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: crpp_make_icon <in.png> <out.ico>\n");
    return 2;
  }
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ui::ComPtr<IWICImagingFactory> wic;
  ui::ComPtr<IWICBitmapDecoder> decoder;
  ui::ComPtr<IWICBitmapFrameDecode> decoded;
  ui::ComPtr<IWICFormatConverter> source;
  const std::string name(argv[1]);
  const std::wstring path(name.begin(), name.end());
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                              reinterpret_cast<void**>(wic.put()))) ||
      FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put())) ||
      FAILED(decoder->GetFrame(0, decoded.put())) || FAILED(wic->CreateFormatConverter(source.put())) ||
      FAILED(source->Initialize(decoded.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom))) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }
  std::vector<Bytes> frames;
  const std::vector<std::uint32_t> sizes{16, 20, 24, 32, 40, 48, 256};
  for (const std::uint32_t size : sizes) {
    const auto pixels = scaled(wic.get(), source.get(), size);
    if (pixels.empty()) return 1;
    frames.push_back(size == 256 ? png(pixels, size) : dib(pixels, size));
    if (frames.back().empty()) return 1;
  }
  source.reset();
  decoded.reset();
  decoder.reset();
  wic.reset();
  if (SUCCEEDED(com)) CoUninitialize();

  Bytes file;
  put16(file, 0);
  put16(file, 1);
  put16(file, static_cast<std::uint32_t>(frames.size()));
  auto offset = static_cast<std::uint32_t>(6 + 16 * frames.size());
  for (std::size_t i = 0; i < frames.size(); ++i) {
    file.push_back(static_cast<char>(sizes[i] & 0xFF));  // 256 is written as 0
    file.push_back(static_cast<char>(sizes[i] & 0xFF));
    put16(file, 0);
    put16(file, 1);
    put16(file, 32);
    put32(file, static_cast<std::uint32_t>(frames[i].size()));
    put32(file, offset);
    offset += static_cast<std::uint32_t>(frames[i].size());
  }
  for (const Bytes& frame : frames) file.insert(file.end(), frame.begin(), frame.end());
  std::ofstream out(argv[2], std::ios::binary);
  out.write(file.data(), static_cast<std::streamsize>(file.size()));
  return out ? 0 : 1;
}
