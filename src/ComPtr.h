#pragma once

#include <unknwn.h>
#include <utility>

// Minimal RAII holder for COM interface pointers. Not a COM framework -
// just enough to avoid manual Release() bookkeeping at every early return.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* p) noexcept : ptr_(p) {}
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }

    T* operator->() const noexcept { return ptr_; }
    T** addressOf() noexcept { return &ptr_; }
    T* get() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    void reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};
