#pragma once
// Owns a Windows handle or a block of system memory and frees it with the function the API
// names, so no early return can leak it. Free of Windows headers: the caller names the type and
// the function, as in `Own<HANDLE, CloseHandle>` or `Own<HICON, DestroyIcon>`.
#include <memory>
#include <type_traits>

namespace crpp::win {

template <auto Free>
struct Freer {
  template <class P>
  void operator()(P* p) const noexcept {
    (void)Free(p);
  }
};

// `Handle` is the API's pointer type (HANDLE, HICON, WCHAR* and so on); null means none.
template <class Handle, auto Free>
  requires std::is_pointer_v<Handle>
using Own = std::unique_ptr<std::remove_pointer_t<Handle>, Freer<Free>>;

}  // namespace crpp::win
