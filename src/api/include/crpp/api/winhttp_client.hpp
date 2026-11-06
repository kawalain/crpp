#pragma once
// HttpClient over WinHTTP. Connects on the first request, so creating one never
// blocks; use it from one thread at a time.
#include <memory>
#include <string_view>

#include "crpp/api/api.hpp"

namespace crpp::api {

class WinHttpClient final : public HttpClient {
 public:
  explicit WinHttpClient(std::string_view server_url);
  ~WinHttpClient();
  [[nodiscard]] HttpResult send(const Request& request) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crpp::api
