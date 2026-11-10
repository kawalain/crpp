#pragma once
// A minimal COM smart pointer, so no adapter depends on WRL or ATL.
#include <utility>

namespace crpp::win {

template <class T>
class ComPtr {
 public:
  ComPtr() = default;
  ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) p_->AddRef(); }
  ComPtr(ComPtr&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
  ~ComPtr() { reset(); }
  ComPtr& operator=(ComPtr o) noexcept { std::swap(p_, o.p_); return *this; }

  void reset() {
    if (p_) p_->Release();
    p_ = nullptr;
  }
  [[nodiscard]] T* get() const { return p_; }
  T* operator->() const { return p_; }
  explicit operator bool() const { return p_ != nullptr; }
  // A base-interface pointer to the same object.
  template <class U>
  [[nodiscard]] ComPtr<U> upcast() const {
    ComPtr<U> r;
    if (p_) {
      p_->AddRef();
      *r.put() = static_cast<U*>(p_);
    }
    return r;
  }
  // For out-parameters: releases the current pointer first.
  [[nodiscard]] T** put() {
    reset();
    return &p_;
  }
  // For out-parameters typed void**, as CoCreateInstance and QueryInterface take.
  [[nodiscard]] void** put_void() { return reinterpret_cast<void**>(put()); }

 private:
  T* p_ = nullptr;
};

}  // namespace crpp::win
