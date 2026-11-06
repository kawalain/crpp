// crpp_update_verify <manifest> <exe> [public key hex]
// Runs the checks the app will run, against the key built into this build unless one
// is named. The release workflow calls it before publishing, so a secret that does not
// match the key in the repository stops the release instead of every later update.
#include <cstdio>
#include <string>

#include "crpp/update/built_in.hpp"
#include "crpp/update/windows_platform.hpp"

using namespace crpp::update;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: crpp_update_verify <manifest> <exe> [public key hex]\n");
    return 2;
  }
  const auto key = argc > 3 ? parse_public_key(argv[3]) : built_in_key();
  if (!key) {
    std::fprintf(stderr, "no public key\n");
    return 1;
  }
  WindowsPlatform platform;
  const auto bytes = platform.read_file(argv[1], 1024);
  if (!bytes) {
    std::fprintf(stderr, "cannot read the manifest\n");
    return 1;
  }
  const auto manifest = parse_manifest(*bytes);
  if (!manifest) {
    std::fprintf(stderr, "bad manifest: %s\n", std::string(error_name(manifest.error())).c_str());
    return 1;
  }
  if (!platform.verify(*key, std::span(*bytes).first(manifest->signed_bytes), manifest->signature)) {
    std::fprintf(stderr, "the signature does not match this key\n");
    return 1;
  }
  const auto exe = platform.read_file(argv[2], static_cast<std::size_t>(manifest->size));
  if (!exe || exe->size() != manifest->size || platform.sha256(*exe) != manifest->sha256) {
    std::fprintf(stderr, "the exe does not match the manifest\n");
    return 1;
  }
  std::printf("verified %s\n", to_string(manifest->version).c_str());
  return 0;
}
