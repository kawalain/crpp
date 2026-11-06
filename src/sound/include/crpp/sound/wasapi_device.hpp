#pragma once
// AudioDevice on Windows: raw WASAPI in shared mode, event driven, on the selected
// render endpoint, or the default one if none is selected or it is gone. Uses the engine's default period (about 10 ms) rather than the
// smallest: network delay dwarfs the difference, and CPU load comes first.
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "crpp/sound/engine.hpp"

namespace crpp::sound {

class WasapiDevice final : public AudioDevice {
 public:
  WasapiDevice();
  ~WasapiDevice();

  [[nodiscard]] std::expected<std::uint32_t, DeviceError> open(Renderer& renderer) override;
  [[nodiscard]] bool start() override;
  void stop() override;
  void close() override;
  void select(std::string_view device) override;
  void set_on_changed(std::function<void()> changed) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

struct OutputDevice {
  std::string id;    // opaque; what WasapiDevice::select takes
  std::string name;  // for people, in UTF-8
};

// The render endpoints in use now, and word of when that changes. Windows calls back
// on its own thread, so nothing is ever polled. For one thread.
class OutputDevices {
 public:
  OutputDevices();
  ~OutputDevices();
  OutputDevices(const OutputDevices&) = delete;
  OutputDevices& operator=(const OutputDevices&) = delete;

  // Empty if Windows has none or cannot say.
  [[nodiscard]] std::vector<OutputDevice> list() const;
  // Once. `changed` runs on a system thread and must not block; the watch ends with
  // this object.
  void watch(std::function<void()> changed);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crpp::sound
