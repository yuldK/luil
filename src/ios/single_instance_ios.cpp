#include "luil/app/platform_services.h"

#include <memory>
#include <string>

// 단일 인스턴스의 iOS 쪽이다 (platform-services-design.md). OS가 앱마다 프로세스를 하나로 묶는다 — 이 프로세스가 곧 첫째다.
//  - 앱을 다시 여는 요청은 장면의 URL·사용자 활동으로 오고 이 길이 아니다 (Android의 Intent와 같은 자리).
namespace luil {
    namespace {
        [[nodiscard]] platform_result unsupported(const std::u8string_view why)
        {
            return { platform_status::unsupported, std::u8string { why }, std::nullopt };
        }
    }

    struct single_instance::state
    {
        std::u8string name {};
        platform_result status {};
    };

    single_instance::single_instance(const std::u8string_view name)
        : state_ { std::make_unique<state>() }
    {
        state_->name = name;
        state_->status = unsupported(u8"iOS keeps one process per app; every instance is the primary one.");
    }

    single_instance::single_instance(single_instance&&) noexcept = default;
    single_instance& single_instance::operator=(single_instance&&) noexcept = default;
    single_instance::~single_instance() = default;

    bool single_instance::primary() const noexcept
    {
        return state_ != nullptr;
    }

    const platform_result& single_instance::status() const noexcept
    {
        static const platform_result moved { platform_status::failed, u8"The instance guard was moved from.", std::nullopt };
        return state_ != nullptr ? state_->status : moved;
    }

    std::u8string_view single_instance::name() const noexcept
    {
        return state_ != nullptr ? std::u8string_view { state_->name } : std::u8string_view {};
    }

    platform_result single_instance::send_to_primary(const std::span<const std::u8string> arguments, const instance_send_options& options) const
    {
        static_cast<void>(arguments);
        static_cast<void>(options);
        // `primary()`가 늘 참이어도 `not_found`보다 `unsupported`가 먼저다 (Android와 같다).
        return unsupported(u8"iOS keeps one process per app; there is no second instance to send arguments from.");
    }
}
