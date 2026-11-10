#pragma once
// The handles and system memory the adapters own, each freed by the function its API names.
// Includes <windows.h>: a target that uses it defines NOMINMAX and WIN32_LEAN_AND_MEAN.
#include <windows.h>

#include <objbase.h>

#include "crpp/win/own.hpp"

namespace crpp::win {

// Plain functions, as an import's own address is not a constant a template can take.
namespace free_with {
inline BOOL close_handle(HANDLE handle) noexcept { return CloseHandle(handle); }
inline void co_task_mem_free(void* memory) noexcept { CoTaskMemFree(memory); }
inline HLOCAL local_free(void* memory) noexcept { return LocalFree(memory); }
inline BOOL delete_object(HGDIOBJ object) noexcept { return DeleteObject(object); }
inline BOOL destroy_icon(HICON icon) noexcept { return DestroyIcon(icon); }
inline BOOL delete_dc(HDC dc) noexcept { return DeleteDC(dc); }
inline BOOL destroy_menu(HMENU menu) noexcept { return DestroyMenu(menu); }
inline BOOL free_library(HMODULE module) noexcept { return FreeLibrary(module); }
}  // namespace free_with

// A kernel handle: an event, a file, a process.
using Handle = Own<HANDLE, free_with::close_handle>;
// Takes both spellings of "no handle" that kernel calls return, null and INVALID_HANDLE_VALUE.
[[nodiscard]] inline Handle own_handle(HANDLE handle) noexcept {
  return Handle(handle == INVALID_HANDLE_VALUE ? nullptr : handle);
}

// Memory a call allocated with CoTaskMemAlloc, or with LocalAlloc, for the caller to free.
template <class T>
using CoTaskMem = Own<T*, free_with::co_task_mem_free>;
template <class T>
using LocalMem = Own<T*, free_with::local_free>;

// A GDI object (HBITMAP, HRGN and so on), a memory device context, an icon and a menu.
template <class H>
using GdiObject = Own<H, free_with::delete_object>;
using MemoryDc = Own<HDC, free_with::delete_dc>;
using Icon = Own<HICON, free_with::destroy_icon>;
using Menu = Own<HMENU, free_with::destroy_menu>;
// A DLL loaded only when it is needed.
using Library = Own<HMODULE, free_with::free_library>;

}  // namespace crpp::win
