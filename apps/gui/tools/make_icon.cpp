// crpp_make_icon <in.svg> <out.ico>: renders the app's icon at the sizes Windows asks
// for. Run by hand when the drawing changes; the result is committed, so a build needs
// no rendering step. Small sizes stay uncompressed, which every loader takes, and the
// largest is PNG, as Windows expects at that size.
#include <wincodec.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "crpp/ui/com.hpp"
#include "crpp/ui/svg.hpp"
#include "tray.hpp"

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

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: crpp_make_icon <in.svg> <out.ico>\n");
    return 2;
  }
  std::ifstream in(argv[1], std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  ui::Gfx gfx;
  if (!gfx.init()) return 1;
  const ui::SvgImage image = ui::parse_svg(gfx, text);
  if (image.shapes.empty()) {
    std::fprintf(stderr, "no shapes in %s\n", argv[1]);
    return 1;
  }
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  std::vector<Bytes> frames;
  const std::vector<std::uint32_t> sizes{16, 20, 24, 32, 40, 48, 256};
  for (const std::uint32_t size : sizes) {
    const auto pixels = gui::render_pixels(gfx.d2d(), static_cast<int>(size), [&](ID2D1RenderTarget* rt) {
      ui::Painter painter(gfx, rt);
      ui::draw_svg(painter, image, {0, 0, static_cast<float>(size), static_cast<float>(size)});
    });
    if (pixels.empty()) return 1;
    frames.push_back(size == 256 ? png(pixels, size) : dib(pixels, size));
    if (frames.back().empty()) return 1;
  }
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
