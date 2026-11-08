// Real BCrypt, WinHTTP against a local server, and real files in a temp folder. The key
// is made here and thrown away; the exe being replaced is a copy of a small stand-in.
#include "crpp/update/windows_platform.hpp"

#include "mock_ws_server.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>

#include "check.hpp"

using namespace crpp::update;

namespace {

std::vector<std::uint8_t> bytes_of(std::string_view s) { return {s.begin(), s.end()}; }

// A P-256 key that exists only for the length of the test.
class ThrowawayKey {
 public:
  ThrowawayKey() {
    BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);
    BCryptGenerateKeyPair(algorithm_, &key_, 256, 0);
    BCryptFinalizeKeyPair(key_, 0);
    std::array<UCHAR, sizeof(BCRYPT_ECCKEY_BLOB) + 64> blob{};
    ULONG size = 0;
    BCryptExportKey(key_, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob.data(), static_cast<ULONG>(blob.size()), &size, 0);
    std::memcpy(public_key.data(), blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), public_key.size());
  }
  ~ThrowawayKey() {
    BCryptDestroyKey(key_);
    BCryptCloseAlgorithmProvider(algorithm_, 0);
  }
  ThrowawayKey(const ThrowawayKey&) = delete;
  ThrowawayKey& operator=(const ThrowawayKey&) = delete;

  [[nodiscard]] Signature sign(std::span<const std::uint8_t> message) const {
    std::array<UCHAR, 32> digest{};
    BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<UCHAR*>(message.data()), static_cast<ULONG>(message.size()),
               digest.data(), static_cast<ULONG>(digest.size()));
    Signature signature{};
    ULONG size = 0;
    BCryptSignHash(key_, nullptr, digest.data(), static_cast<ULONG>(digest.size()), signature.data(),
                   static_cast<ULONG>(signature.size()), &size, 0);
    return signature;
  }

  PublicKey public_key{};

 private:
  BCRYPT_ALG_HANDLE algorithm_ = nullptr;
  BCRYPT_KEY_HANDLE key_ = nullptr;
};

void hashes_like_the_standard() {
  WindowsPlatform platform;
  CHECK(platform.sha256(bytes_of("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void verifies_a_real_signature_and_nothing_else() {
  WindowsPlatform platform;
  const ThrowawayKey key, other;
  const auto message = bytes_of("crpp-update 1\nversion 0.2.0\n");
  const Signature signature = key.sign(message);
  CHECK(platform.verify(key.public_key, message, signature));

  CHECK(!platform.verify(other.public_key, message, signature));
  const auto changed = bytes_of("crpp-update 1\nversion 9.2.0\n");
  CHECK(!platform.verify(key.public_key, changed, signature));
  Signature flipped = signature;
  flipped[10] ^= 1;
  CHECK(!platform.verify(key.public_key, message, flipped));
  CHECK(!platform.verify(key.public_key, message, Signature{}));
  // A point not on the curve cannot be a key.
  CHECK(!platform.verify(PublicKey{1, 2, 3}, message, signature));
}

// Made once with .github/scripts/sign-release.sh and a key that was then discarded: what
// OpenSSL signs, turned from DER into r||s by p256.sh, must be what BCrypt accepts.
void verifies_what_the_release_script_signs() {
  WindowsPlatform platform;
  const std::string text =
      "crpp-update 1\n"
      "version 0.1.0\n"
      "asset crpp.exe\n"
      "size 9\n"
      "sha256 9a42d9e2c95e6916da3ddc7688d75883eb13e8d40fac965069b4c9b2f12953aa\n"
      "signature 5c84fa5fa642599c749c4b8da6ec240488a4f7323b8f3b74e10eb849e8789e0b182d50b7398a521522d42ce7480b880bab64b22d6c7107b6920de2a8b2b08c32\n";
  const auto bytes = bytes_of(text);
  const auto manifest = parse_manifest(bytes);
  CHECK(manifest.has_value());
  const auto key = parse_public_key("c959e4d08b41e195da99b68a245f5d3a04d7b34e48b1dc72443d63f113e84b98902acf2fb079f09b71d52aabed09adddd35d367641230e4c5fd9c77fe1de0fb2");
  CHECK(key.has_value());
  CHECK(platform.verify(*key, std::span(bytes).first(manifest->signed_bytes), manifest->signature));
  CHECK(manifest->sha256 == platform.sha256(bytes_of("fake exe\n")));
}

void reply_with(mock::Conn& c, const std::string& body) {
  const std::string r = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body;
  c.send_all(r.data(), r.size());
}

void gets_a_reply_and_follows_a_redirect() {
  std::string first_line, user_agent, accept;
  std::atomic<int> count{0};
  mock::Server server([&](mock::Conn& c) {
    if (c.request_line().starts_with("GET /api?per_page=30 ")) {
      first_line = c.request_line();
      user_agent = c.header("User-Agent");
      accept = c.header("Accept");
      reply_with(c, "[]");
    } else if (c.request_line().starts_with("GET /moved ")) {
      const std::string r = "HTTP/1.1 302 Found\r\nLocation: /there\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      c.send_all(r.data(), r.size());
    } else {
      ++count;
      reply_with(c, std::string(300000, 'x'));
    }
  });
  const std::string base = "http://127.0.0.1:" + std::to_string(server.port());
  WindowsPlatform platform;
  const auto api = platform.get(base + "/api?per_page=30", 100, "application/vnd.github+json", nullptr);
  CHECK(api && *api == bytes_of("[]"));
  CHECK(!user_agent.empty() && user_agent != "<absent>");
  CHECK(accept == "application/vnd.github+json");

  std::uint64_t last = 0;
  const auto moved = platform.get(base + "/moved", 300000, {}, [&](std::uint64_t received) { last = received; });
  CHECK(moved && moved->size() == 300000);
  CHECK(last == 300000);
  CHECK(count == 1);

  // One byte past the limit is enough to tell.
  const auto longer = platform.get(base + "/x", 10, {}, nullptr);
  CHECK(longer && longer->size() == 11);
}

void failures_say_where_they_happened() {
  mock::Server missing([](mock::Conn& c) { c.reply(404); });
  WindowsPlatform platform;
  CHECK(platform.get("http://127.0.0.1:" + std::to_string(missing.port()) + "/a", 10, {}, nullptr).error() == Error::Unavailable);
  mock::Server limited([](mock::Conn& c) { c.reply(403); });
  CHECK(platform.get("http://127.0.0.1:" + std::to_string(limited.port()) + "/a", 10, {}, nullptr).error() == Error::Unavailable);
  mock::Server drops([](mock::Conn&) {});
  CHECK(platform.get("http://127.0.0.1:" + std::to_string(drops.port()) + "/a", 10, {}, nullptr).error() == Error::Network);
  CHECK(platform.get("not a url", 10, {}, nullptr).error() == Error::Network);
}

// A temp folder with a copy of the stand-in app, which it removes at the end.
class Sandbox {
 public:
  explicit Sandbox(const char* name) : dir(std::filesystem::temp_directory_path() / ("crpp_update_test_" + std::to_string(GetCurrentProcessId()) + name)) {
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    exe = dir / "crpp.exe";
    std::filesystem::copy_file(CRPP_FAKE_APP, exe);
    original = read(exe);
  }
  ~Sandbox() {
    // A child may still be on its way out.
    for (int i = 0; i < 100; ++i) {
      std::error_code error;
      std::filesystem::remove_all(dir, error);
      if (!error) return;
      Sleep(100);
    }
  }
  [[nodiscard]] static std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  }
  [[nodiscard]] std::vector<std::uint8_t> newer() const {
    // Bytes after the image change the hash and nothing else.
    auto bytes = original;
    bytes.push_back('!');
    return bytes;
  }

  std::filesystem::path dir, exe;
  std::vector<std::uint8_t> original;
};

// Runs the copy as if it were the app whose exe is about to be replaced.
class RunningCopy {
 public:
  explicit RunningCopy(const std::filesystem::path& exe) {
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process_);
  }
  ~RunningCopy() {
    TerminateProcess(process_.hProcess, 0);
    WaitForSingleObject(process_.hProcess, 5000);
    CloseHandle(process_.hProcess);
    CloseHandle(process_.hThread);
  }
  RunningCopy(const RunningCopy&) = delete;
  RunningCopy& operator=(const RunningCopy&) = delete;

 private:
  PROCESS_INFORMATION process_{};
};

void replaces_the_exe_of_a_running_process() {
  Sandbox box("replace");
  WindowsPlatform platform;
  const RunningCopy running(box.exe);
  const auto bytes = box.newer();
  const auto result = self_replace(box.exe, bytes, platform.sha256(bytes), platform);
  CHECK(result.has_value());
  CHECK(Sandbox::read(box.exe) == bytes);
  CHECK(Sandbox::read(std::filesystem::path(box.exe) += ".old") == box.original);
  CHECK(!std::filesystem::exists(std::filesystem::path(box.exe) += ".new"));
}

void puts_the_old_exe_back_when_the_new_one_crashes() {
  Sandbox box("crash");
  WindowsPlatform platform(L"--crash");
  const auto bytes = box.newer();
  CHECK(self_replace(box.exe, bytes, platform.sha256(bytes), platform).error() == Error::Launch);
  CHECK(Sandbox::read(box.exe) == box.original);
  CHECK(!std::filesystem::exists(std::filesystem::path(box.exe) += ".new"));
  CHECK(!std::filesystem::exists(std::filesystem::path(box.exe) += ".old"));
}

void puts_the_old_exe_back_when_the_new_one_never_answers() {
  Sandbox box("silent");
  WindowsPlatform platform(L"--silent", 500);
  const auto bytes = box.newer();
  CHECK(self_replace(box.exe, bytes, platform.sha256(bytes), platform).error() == Error::Launch);
  CHECK(Sandbox::read(box.exe) == box.original);
  CHECK(!std::filesystem::exists(std::filesystem::path(box.exe) += ".old"));
}

void puts_the_old_exe_back_when_the_new_one_is_not_a_program() {
  Sandbox box("garbage");
  WindowsPlatform platform;
  const auto bytes = bytes_of("this is not an executable");
  CHECK(self_replace(box.exe, bytes, platform.sha256(bytes), platform).error() == Error::Launch);
  CHECK(Sandbox::read(box.exe) == box.original);
}

void a_hash_that_does_not_match_never_reaches_the_exe() {
  Sandbox box("hash");
  WindowsPlatform platform;
  CHECK(self_replace(box.exe, box.newer(), std::string(64, 'a'), platform).error() == Error::Disk);
  CHECK(Sandbox::read(box.exe) == box.original);
  CHECK(!std::filesystem::exists(std::filesystem::path(box.exe) += ".new"));
}

void removes_leftovers() {
  Sandbox box("left");
  WindowsPlatform platform;
  const auto fresh = std::filesystem::path(box.exe) += ".new", old = std::filesystem::path(box.exe) += ".old";
  CHECK(platform.write_file(fresh, bytes_of("x")));
  CHECK(platform.write_file(old, bytes_of("x")));
  remove_leftovers(box.exe, platform);
  CHECK(!std::filesystem::exists(fresh) && !std::filesystem::exists(old));
  CHECK(std::filesystem::exists(box.exe));
  remove_leftovers(box.exe, platform);  // nothing there is fine too
}

void files_round_trip() {
  Sandbox box("files");
  WindowsPlatform platform;
  const auto path = box.dir / "a.bin";
  const std::vector<std::uint8_t> bytes(3 * 1024 * 1024, 7);
  CHECK(platform.write_file(path, bytes));
  CHECK(platform.read_file(path, bytes.size()).value() == bytes);
  CHECK(platform.read_file(path, 10).error() == Error::Disk);
  CHECK(platform.read_file(box.dir / "none", 10).error() == Error::Disk);
  CHECK(!platform.write_file(box.dir / "no" / "dir" / "a", bytes));
  CHECK(!platform.move_file(box.dir / "none", box.dir / "b"));
  CHECK(platform.remove_file(box.dir / "none"));
}

}  // namespace

int main() {
  hashes_like_the_standard();
  verifies_a_real_signature_and_nothing_else();
  verifies_what_the_release_script_signs();
  gets_a_reply_and_follows_a_redirect();
  failures_say_where_they_happened();
  replaces_the_exe_of_a_running_process();
  puts_the_old_exe_back_when_the_new_one_crashes();
  puts_the_old_exe_back_when_the_new_one_never_answers();
  puts_the_old_exe_back_when_the_new_one_is_not_a_program();
  a_hash_that_does_not_match_never_reaches_the_exe();
  removes_leftovers();
  files_round_trip();
  return crpp_test_result();
}
