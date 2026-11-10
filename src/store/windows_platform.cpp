#include "crpp/store/windows_platform.hpp"

#include <windows.h>

#include <dpapi.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <limits>

#include "crpp/win/handles.hpp"

namespace crpp::store {

namespace {

// Another program (an antivirus scanner, say) can briefly hold the target open.
constexpr int replace_attempts = 3;
constexpr DWORD replace_retry_ms = 50;

DATA_BLOB blob_of(std::string_view bytes) noexcept {
  DATA_BLOB blob{};
  blob.cbData = static_cast<DWORD>(bytes.size());
  blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data()));
  return blob;
}

std::string take_blob(DATA_BLOB& blob) {
  const win::LocalMem<BYTE> owned(blob.pbData);
  return std::string(reinterpret_cast<const char*>(owned.get()), blob.cbData);
}

bool fits_dword(std::string_view bytes) noexcept { return bytes.size() <= std::numeric_limits<DWORD>::max(); }

bool write_and_flush(const std::filesystem::path& path, std::string_view bytes) noexcept {
  const auto file = win::own_handle(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file) return false;
  DWORD written = 0;
  // Flushing first: the rename's write-through flag does not cover the data.
  return WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
         written == bytes.size() && FlushFileBuffers(file.get());
}

}  // namespace

std::expected<std::string, Error> WindowsPlatform::protect(std::string_view plain) {
  if (!fits_dword(plain)) return std::unexpected(Error::Protect);
  DATA_BLOB in = blob_of(plain);
  DATA_BLOB out{};
  if (!CryptProtectData(&in, L"crpp", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
    return std::unexpected(Error::Protect);
  }
  return take_blob(out);
}

std::expected<std::string, Error> WindowsPlatform::unprotect(std::string_view blob) {
  if (!fits_dword(blob)) return std::unexpected(Error::Protect);
  DATA_BLOB in = blob_of(blob);
  DATA_BLOB out{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
    return std::unexpected(Error::Protect);
  }
  return take_blob(out);
}

std::expected<void, Error> WindowsPlatform::replace_file(const std::filesystem::path& path, std::string_view bytes) {
  if (!fits_dword(bytes)) return std::unexpected(Error::Write);
  std::filesystem::path temp = path;
  temp += L".tmp";
  if (!write_and_flush(temp, bytes)) {
    DeleteFileW(temp.c_str());
    return std::unexpected(Error::Write);
  }
  for (int attempt = 0; attempt < replace_attempts; ++attempt) {
    if (attempt > 0) Sleep(replace_retry_ms);
    if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return {};
  }
  DeleteFileW(temp.c_str());
  return std::unexpected(Error::Write);
}

std::expected<Locations, Error> default_locations() {
  PWSTR raw = nullptr;
  const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw);
  const win::CoTaskMem<WCHAR> owned(raw);  // freed whether or not the call succeeded
  if (FAILED(hr) || !owned) return std::unexpected(Error::BadData);
  const std::filesystem::path base = owned.get();
  return Locations{base / "crpp", base / "Cloudroom"};
}

}  // namespace crpp::store
