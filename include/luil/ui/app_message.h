#pragma once

#include <memory>
#include <typeinfo>
#include <utility>

namespace luil {
    // 앱 메시지의 불투명 운반체다.
    // UI element의 액션은 앱이 정의한 메시지 타입을 이 상자에 담아 반환하고,
    // 라이브러리는 내용을 해석하지 않은 채 앱이 지정한 배출구(logic inbox)로 전달만 한다.
    // 앱은 경계에서 `get`으로 복원한다.
    //
    // 값은 공유 소유라 복사가 싸고, channel을 값으로 지나도 payload 복사가 없다.
    // 담은 값은 불변으로 취급한다.
    //  - 게시된 tree의 액션이 여러 스레드에서 실행될 수 있기 때문이다.
    class app_message
    {
    public:
        app_message() = default;

        template<typename message_type>
        explicit app_message(message_type value)
            : value_ { std::make_shared<const message_type>(std::move(value)) }
            , type_ { &typeid(message_type) }
        {}

        [[nodiscard]] bool empty() const noexcept
        {
            return value_ == nullptr;
        }

        // 담긴 타입이 다르면 nullptr다.
        // 앱 경계에서 알려진 타입 하나로 복원하는 용도라 계층 변환은 지원하지 않는다.
        template<typename message_type>
        [[nodiscard]] const message_type* get() const noexcept
        {
            if (type_ == nullptr || *type_ != typeid(message_type))
                return nullptr;
            return static_cast<const message_type*>(value_.get());
        }

    private:
        std::shared_ptr<const void> value_ {};
        const std::type_info* type_ { nullptr };
    };
} // namespace luil
