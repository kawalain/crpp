#include "crpp/assets/windows_platform.hpp"

#include <fstream>
#include <iterator>

#include "check.hpp"
#include "crpp/assets/built_in.hpp"
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
  WindowsPlatform platform("http://127.0.0.1/");
  // FIPS 180-2 examples.
  CHECK(platform.sha256(bytes_of("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(platform.sha256({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

void fetches_a_file_and_reports_progress() {
  std::string line;
  mock::Server server([&](mock::Conn& c) {
    line = c.request_line();
    reply_with(c, std::string(100000, 'x'));
  });
  WindowsPlatform platform(base_url(server));
  std::uint64_t last = 0;
  const auto bytes = platform.fetch({"sounds/a.wav", 100000, ""}, "sounds/a.wav", [&](std::uint64_t received) { last = received; });
  CHECK(bytes && bytes->size() == 100000);
  CHECK(last == 100000);
  CHECK(line.starts_with("GET /base/sounds/a.wav "));
}

void fetches_from_several_threads_at_once() {
  mock::Server server([](mock::Conn& c) { reply_with(c, std::string(5000, 'x')); });
  WindowsPlatform platform(base_url(server));
  std::atomic<int> good{0};
  {
    std::vector<std::jthread> threads;
    for (int i = 0; i < 8; ++i) {
      threads.emplace_back([&] {
        for (int n = 0; n < 20; ++n) {
          const auto bytes = platform.fetch({"a", 5000, ""}, "a", nullptr);
          if (bytes && bytes->size() == 5000 && platform.sha256(*bytes).size() == 64) ++good;
        }
      });
    }
  }
  CHECK(good == 160);
}

void a_longer_reply_stops_one_byte_past_the_file() {
  mock::Server server([](mock::Conn& c) { reply_with(c, std::string(5000, 'x')); });
  WindowsPlatform platform(base_url(server));
  const auto bytes = platform.fetch({"a", 10, ""}, "a", nullptr);
  CHECK(bytes && bytes->size() == 11);
}

void failures_say_where_they_happened() {
  mock::Server server([](mock::Conn& c) { c.reply(404); });
  WindowsPlatform not_found(base_url(server));
  CHECK(not_found.fetch({"a", 1, ""}, "a", nullptr).error() == Error::Unavailable);

  // The server dropped the connection before answering.
  mock::Server drops([](mock::Conn&) {});
  WindowsPlatform dropped(base_url(drops));
  CHECK(dropped.fetch({"a", 1, ""}, "a", nullptr).error() == Error::Network);

  WindowsPlatform nonsense("not a url");
  CHECK(nonsense.fetch({"a", 1, ""}, "a", nullptr).error() == Error::Network);
}

void the_built_in_manifest_describes_the_assets_folder() {
  const Manifest manifest = built_in_manifest();
  CHECK(manifest.entries.size() > 100);
  CHECK(published_url() == "https://cdn.jsdelivr.net/gh/");
  WindowsPlatform platform("http://127.0.0.1/");
  // Text files, which are what a line ending conversion would change.
  int checked = 0;
  for (const Entry& entry : manifest.entries) {
    if (entry.path != "words/animals.txt" && entry.path != "words/adjectives.txt") continue;
    std::ifstream in(std::filesystem::path(CRPP_ASSETS_DIR) / entry.path, std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    CHECK(bytes.size() == entry.size);
    CHECK(platform.sha256(bytes) == entry.sha256);
    ++checked;
  }
  CHECK(checked == 2);
}

}  // namespace

int main() {
  hashes_like_the_standard();
  fetches_a_file_and_reports_progress();
  fetches_from_several_threads_at_once();
  a_longer_reply_stops_one_byte_past_the_file();
  failures_say_where_they_happened();
  the_built_in_manifest_describes_the_assets_folder();
  return crpp_test_result();
}
