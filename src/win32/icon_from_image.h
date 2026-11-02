#pragma once

#include "luil/ui/image_element.h"

#include <windows.h>

#include <string>
#include <utility>

namespace luil::win32 {
    // `HICON` 하나를 쥐고 없어질 때 `DestroyIcon`으로 거둔다.
    //  - `owned_handle`은 `CloseHandle`용이라 아이콘에 맞지 않는다. 셸이 아이콘을 복사하는지 참조하는지에 기대지 않고
    //    쓰는 동안은 우리가 쥐며, 교체·해제·창 파괴까지 놓지 않는다.
    class owned_icon
    {
    public:
        owned_icon() noexcept = default;
        explicit owned_icon(const HICON icon) noexcept
            : icon_ { icon }
        {
        }
        owned_icon(const owned_icon&) = delete;
        owned_icon& operator=(const owned_icon&) = delete;
        owned_icon(owned_icon&& other) noexcept
            : icon_ { std::exchange(other.icon_, nullptr) }
        {
        }
        owned_icon& operator=(owned_icon&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                icon_ = std::exchange(other.icon_, nullptr);
            }
            return *this;
        }
        ~owned_icon()
        {
            reset();
        }

        [[nodiscard]] HICON get() const noexcept
        {
            return icon_;
        }

        [[nodiscard]] bool valid() const noexcept
        {
            return icon_ != nullptr;
        }

        void reset() noexcept
        {
            if (icon_ != nullptr)
                static_cast<void>(DestroyIcon(icon_));
            icon_ = nullptr;
        }

    private:
        HICON icon_ { nullptr };
    };

    // `ui_image`를 `size`×`size` 픽셀의 알파 `HICON`으로 옮긴다. 실패하면 빈 아이콘과 `error`다.
    //  - 원본이 정사각형이 아니면 비율을 지켜 가운데에 맞추고 남는 곳은 투명하다. 크기가 다르면 이 크기로 맞춘다.
    //  - 작업 표시줄 오버레이와 tray가 크기만 달리해 같은 함수를 쓴다.
    [[nodiscard]] owned_icon make_icon(const ui_image& image, int size, std::u8string& error);
}
