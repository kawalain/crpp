#pragma once
// AudioDevice on Windows: raw WASAPI in shared mode, event driven, on the default
// render endpoint. Uses the engine's default period (about 10 ms) rather than the
// smallest: network delay dwarfs the difference, and CPU load comes first.
#include <memory>

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
  void set_on_changed(std::function<void()> changed) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crpp::sound
