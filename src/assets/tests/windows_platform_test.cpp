#include "crpp/assets/windows_platform.hpp"

#include "check.hpp"
#include "mock_ws_server.hpp"

using namespace crpp::assets;

namespace {

void reply_with(mock::Conn& c, const std::string& body) {
  const std::string r = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body;
  c.send_all(r.data(), r.size());
}

std::string base_url(const mock::Server& server) { return "http://127.0.0.1:" + std::to_string(server.port()) + "/base/"; }

std::vector<std::uint8_t> bytes_of(std::string_view s) { return {s.begin(), s.end()}; }

void hashes_like_the_standard() {
  WindowsPlatform platform;
  // FIPS 180-2 examples.
  CHECK(platform.sha256(bytes_of("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(platform.sha256({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

void fetches_a_file_and_reports_progress() {
  std::string line;
  mock::Server server([&](mock::Conn& c) {
    line = c.request_line();
    reply_with(c, std::string(200000, 'x'));
  });
  WindowsPlatform platform;
  std::vector<std::uint64_t> steps;
  const auto bytes = platform.fetch(base_url(server) + "sounds/a.wav", 200000, [&](std::uint64_t received) { steps.push_back(received); });
  CHECK(bytes && bytes->size() == 200000);
  CHECK(!steps.empty() && steps.back() == 200000);
  // In small reads, so that a large file moves the bar while it arrives.
  CHECK(steps.size() >= 4);
  CHECK(line.starts_with("GET /base/sounds/a.wav "));
}

void fetches_from_several_threads_at_once() {
  mock::Server server([](mock::Conn& c) { reply_with(c, std::string(5000, 'x')); });
  WindowsPlatform platform;
  const std::string url = base_url(server) + "a";
  std::atomic<int> good{0};
  {
    std::vector<std::jthread> threads;
    for (int i = 0; i < 8; ++i) {
      threads.emplace_back([&] {
        for (int n = 0; n < 20; ++n) {
          const auto bytes = platform.fetch(url, 5000, nullptr);
          if (bytes && bytes->size() == 5000 && platform.sha256(*bytes).size() == 64) ++good;
        }
      });
    }
  }
  CHECK(good == 160);
}

void a_longer_reply_stops_one_byte_past_the_limit() {
  mock::Server server([](mock::Conn& c) { reply_with(c, std::string(5000, 'x')); });
  WindowsPlatform platform;
  const auto bytes = platform.fetch(base_url(server) + "a", 10, nullptr);
  CHECK(bytes && bytes->size() == 11);
}

void failures_say_where_they_happened() {
  mock::Server server([](mock::Conn& c) { c.reply(404); });
  WindowsPlatform platform;
  CHECK(platform.fetch(base_url(server) + "a", 1, nullptr).error() == Error::Unavailable);

  // The server dropped the connection before answering.
  mock::Server drops([](mock::Conn&) {});
  CHECK(platform.fetch(base_url(drops) + "a", 1, nullptr).error() == Error::Network);

  CHECK(platform.fetch("not a url", 1, nullptr).error() == Error::Network);
}

void verifies_with_the_update_key_check() {
  WindowsPlatform platform;
  const std::vector<std::uint8_t> message{1, 2, 3};
  // Real signatures are tested in the update module; this one only has to be refused.
  CHECK(!platform.verify(crpp::update::PublicKey{1}, message, crpp::update::Signature{}));
}

}  // namespace

int main() {
  hashes_like_the_standard();
  fetches_a_file_and_reports_progress();
  fetches_from_several_threads_at_once();
  a_longer_reply_stops_one_byte_past_the_limit();
  failures_say_where_they_happened();
  verifies_with_the_update_key_check();
  return crpp_test_result();
}
