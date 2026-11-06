#pragma once
// A minimal COM smart pointer so the UI does not depend on WRL/ATL.
#include <utility>

namespace crpp::ui {

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
  // For out-parameters: releases the current pointer first.
  [[nodiscard]] T** put() {
    reset();
    return &p_;
  }

 private:
  T* p_ = nullptr;
};

}  // namespace crpp::ui
