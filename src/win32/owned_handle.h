#pragma once

#include <windows.h>

#include <utility>

namespace luil::win32 {
    // 핸들 하나를 쥐고 없어질 때 닫는다.
    //  - 플랫폼 도구(파일·잠금·프로세스)와 파일 감시(폴더 핸들·완료 포트)가 함께 쓴다. `CloseHandle`로 닫는 핸들만이다.
    class owned_handle
    {
    public:
        owned_handle() noexcept = default;
        explicit owned_handle(const HANDLE handle) noexcept
            : handle_ { handle }
        {
        }
        owned_handle(const owned_handle&) = delete;
        owned_handle& operator=(const owned_handle&) = delete;
        owned_handle(owned_handle&& other) noexcept
            : handle_ { std::exchange(other.handle_, nullptr) }
        {
        }
        owned_handle& operator=(owned_handle&& other) noexcept
        {
            if (this != &other)
            {
                close();
                handle_ = std::exchange(other.handle_, nullptr);
            }
            return *this;
        }
        ~owned_handle()
        {
            close();
        }

        [[nodiscard]] HANDLE get() const noexcept
        {
            return handle_;
        }

        [[nodiscard]] bool valid() const noexcept
        {
            return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
        }

    private:
        void close() noexcept
        {
            if (valid())
                static_cast<void>(CloseHandle(handle_));
            handle_ = nullptr;
        }

        HANDLE handle_ { nullptr };
    };
}
