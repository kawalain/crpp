#include "crpp/sound/wasapi_device.hpp"

#include <windows.h>

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <xmmintrin.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace crpp::sound {

namespace {

template <typename T>
void release(T*& p) noexcept {
  if (p) p->Release();
  p = nullptr;
}

enum class SampleType : std::uint8_t { Float32, Int16 };

}  // namespace

struct WasapiDevice::Impl final : IMMNotificationClient {
  std::mutex changed_mutex;
  std::function<void()> changed;
  void notify_changed() {
    std::lock_guard lock(changed_mutex);
    if (changed) changed();
  }

  // ---- IMMNotificationClient: only sets a flag, as its rules require ----
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
      *out = static_cast<IMMNotificationClient*>(this);
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
    if (flow == eRender && role == eConsole) notify_changed();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

  // ---- the stream ----
  bool com_ready = false;
  IMMDeviceEnumerator* enumerator = nullptr;
  bool registered = false;
  IAudioClient* client = nullptr;
  IAudioRenderClient* render_client = nullptr;
  HANDLE ready_event = nullptr;
  HANDLE stop_event = nullptr;
  UINT32 buffer_frames = 0;
  std::size_t channels = 0;
  SampleType type = SampleType::Float32;
  Renderer* renderer = nullptr;
  std::vector<float> stereo;
  std::thread thread;

  void write_frames(BYTE* out, UINT32 frames) noexcept {
    const std::span<float> mix(stereo.data(), std::size_t{frames} * 2);
    renderer->render(mix);
    for (std::size_t f = 0; f < frames; ++f) {
      const float left = mix[f * 2], right = mix[f * 2 + 1];
      for (std::size_t c = 0; c < channels; ++c) {
        // Mono takes both sides; extra channels (surround) stay silent.
        const float value = channels == 1 ? (left + right) * 0.5f : c == 0 ? left : c == 1 ? right : 0.0f;
        if (type == SampleType::Float32) {
          reinterpret_cast<float*>(out)[f * channels + c] = value;
        } else {
          reinterpret_cast<std::int16_t*>(out)[f * channels + c] =
              static_cast<std::int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767.0f);
        }
      }
    }
  }

  void render_loop() noexcept {
    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD task_index = 0;
    const HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    // Decaying reverb reaches denormals, which are very slow on the CPU.
    _mm_setcsr(_mm_getcsr() | 0x8040u);  // FTZ and DAZ

    const HANDLE waits[2] = {stop_event, ready_event};
    bool lost = false;
    while (!lost && WaitForMultipleObjects(2, waits, FALSE, 2000) != WAIT_OBJECT_0) {
      UINT32 padding = 0;
      HRESULT hr = client->GetCurrentPadding(&padding);
      if (SUCCEEDED(hr) && buffer_frames > padding) {
        const UINT32 frames = buffer_frames - padding;
        BYTE* data = nullptr;
        hr = render_client->GetBuffer(frames, &data);
        if (SUCCEEDED(hr)) {
          write_frames(data, frames);
          hr = render_client->ReleaseBuffer(frames, 0);
        }
      }
      lost = FAILED(hr);
    }
    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
    if (lost) notify_changed();  // the device was removed or changed its format
  }

  void close_stream() noexcept {
    release(render_client);
    release(client);
    if (ready_event) CloseHandle(ready_event);
    if (stop_event) CloseHandle(stop_event);
    ready_event = stop_event = nullptr;
    stereo = {};
    renderer = nullptr;
  }
};

WasapiDevice::WasapiDevice() : impl_(std::make_unique<Impl>()) {}

WasapiDevice::~WasapiDevice() { close(); }

void WasapiDevice::set_on_changed(std::function<void()> changed) {
  std::lock_guard lock(impl_->changed_mutex);
  impl_->changed = std::move(changed);
}

std::expected<std::uint32_t, DeviceError> WasapiDevice::open(Renderer& renderer) {
  Impl& s = *impl_;
  close();
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  s.com_ready = SUCCEEDED(init);  // each success pairs with a CoUninitialize in close()

  const auto fail = [&](DeviceError error) -> std::expected<std::uint32_t, DeviceError> {
    close();
    return std::unexpected(error);
  };
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void**>(&s.enumerator)))) {
    return fail(DeviceError::Unavailable);
  }
  s.registered = SUCCEEDED(s.enumerator->RegisterEndpointNotificationCallback(&s));

  IMMDevice* endpoint = nullptr;
  HRESULT hr = s.enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint);
  if (SUCCEEDED(hr)) hr = endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&s.client));
  release(endpoint);
  if (FAILED(hr)) return fail(DeviceError::Unavailable);

  WAVEFORMATEX* format = nullptr;
  if (FAILED(s.client->GetMixFormat(&format))) return fail(DeviceError::Unavailable);
  bool is_float = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
  bool is_pcm = format->wFormatTag == WAVE_FORMAT_PCM;
  if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
    // The GUID's first field holds the plain format tag for both of these subtypes.
    const auto subtype = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->SubFormat.Data1;
    is_float = subtype == WAVE_FORMAT_IEEE_FLOAT;
    is_pcm = subtype == WAVE_FORMAT_PCM;
  }
  const std::uint32_t rate = format->nSamplesPerSec;
  s.channels = format->nChannels;
  const WORD bits = format->wBitsPerSample;
  const bool supported = s.channels > 0 && ((is_float && bits == 32) || (is_pcm && bits == 16));
  s.type = is_float ? SampleType::Float32 : SampleType::Int16;
  if (supported) hr = s.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, format, nullptr);
  CoTaskMemFree(format);
  if (!supported) return fail(DeviceError::UnsupportedFormat);
  if (FAILED(hr)) return fail(DeviceError::Unavailable);

  s.ready_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  s.stop_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!s.ready_event || !s.stop_event || FAILED(s.client->SetEventHandle(s.ready_event)) ||
      FAILED(s.client->GetBufferSize(&s.buffer_frames)) ||
      FAILED(s.client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&s.render_client)))) {
    return fail(DeviceError::Unavailable);
  }
  s.stereo.resize(std::size_t{s.buffer_frames} * 2);
  s.renderer = &renderer;
  return rate;
}

bool WasapiDevice::start() {
  Impl& s = *impl_;
  if (!s.client || s.thread.joinable()) return s.thread.joinable();
  // Fill the buffer first, as a stream must not start empty.
  BYTE* data = nullptr;
  if (FAILED(s.render_client->GetBuffer(s.buffer_frames, &data))) return false;
  s.write_frames(data, s.buffer_frames);
  if (FAILED(s.render_client->ReleaseBuffer(s.buffer_frames, 0))) return false;
  if (FAILED(s.client->Start())) return false;
  ResetEvent(s.stop_event);
  s.thread = std::thread([&s] { s.render_loop(); });
  return true;
}

void WasapiDevice::stop() {
  Impl& s = *impl_;
  if (!s.thread.joinable()) return;
  SetEvent(s.stop_event);
  s.thread.join();
  s.client->Stop();
  s.client->Reset();
}

void WasapiDevice::close() {
  Impl& s = *impl_;
  stop();
  s.close_stream();
  if (s.enumerator && s.registered) s.enumerator->UnregisterEndpointNotificationCallback(&s);
  s.registered = false;
  release(s.enumerator);
  if (s.com_ready) CoUninitialize();
  s.com_ready = false;
}

}  // namespace crpp::sound
