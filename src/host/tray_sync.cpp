#include "host/tray_sync.h"

namespace luil {
    namespace {
        [[nodiscard]] bool same_image(const ui_image& left, const ui_image& right) noexcept
        {
            if (left.valid() == false || right.valid() == false)
                return left.valid() == right.valid();
            return left.backing() == right.backing();
        }
    }

    ui_image choose_tray_image(const tray_icon& icon, const bool high_contrast, const bool light_taskbar)
    {
        if (high_contrast && icon.high_contrast_icon.valid())
            return icon.high_contrast_icon;
        if (light_taskbar && icon.light_icon.valid())
            return icon.light_icon;
        return icon.icon;
    }

    void tray_sync::record(const tray_call& call)
    {
        applied_image_ = call.image;
        applied_size_ = call.icon_size;
        applied_text_ = call.text;
    }

    bool tray_sync::differs(const ui_image& image, const int size, const std::u8string& text) const noexcept
    {
        return same_image(applied_image_, image) == false
            || applied_size_ != size
            || applied_text_ != text
        ;
    }

    std::optional<tray_call> tray_sync::update(const std::optional<tray_icon>& wanted, const tray_appearance& appearance)
    {
        if (wanted.has_value() == false)
        {
            // 시간 초과로 섰는지 모르는 아이콘도 지운다. 없으면 셸이 실패를 답할 뿐이다.
            if (phase_ == phase::present || phase_ == phase::waiting_change)
            {
                phase_ = phase::absent;
                state_ = tray_sync_state::off;
                record({});
                return tray_call { .kind = tray_call_kind::remove };
            }
            if (phase_ == phase::waiting_restart)
                state_ = tray_sync_state::off;
            phase_ = phase::absent;
            return std::nullopt;
        }

        tray_call call {
            .kind = tray_call_kind::add
            , .icon = true
            , .tooltip = true
            , .image = choose_tray_image(*wanted, appearance.high_contrast, appearance.light_taskbar)
            , .icon_size = appearance.icon_size
            , .text = wanted->tooltip
        };
        switch (phase_)
        {
        case phase::absent:
            break;
        case phase::waiting_change:
            if (differs(call.image, call.icon_size, call.text) == false)
                return std::nullopt;
            break;
        case phase::waiting_restart:
            state_ = tray_sync_state::waiting;
            return std::nullopt;
        case phase::adding:
        case phase::probing:
            // 결과를 기다리는 동안이다. 플랫폼은 곧바로 `completed`를 부르므로 여기에 오지 않는다.
            return std::nullopt;
        case phase::present: {
            const bool icon { same_image(applied_image_, call.image) == false || applied_size_ != call.icon_size };
            const bool tooltip { applied_text_ != call.text };
            if (icon == false && tooltip == false)
                return std::nullopt;
            call.kind = tray_call_kind::modify;
            call.icon = icon;
            call.tooltip = tooltip;
            record(call);
            return call;
        }
        }
        phase_ = phase::adding;
        record(call);
        return call;
    }

    std::optional<tray_call> tray_sync::completed(const tray_call& call, const tray_call_outcome outcome)
    {
        const bool succeeded { outcome == tray_call_outcome::succeeded };
        switch (call.kind)
        {
        case tray_call_kind::add:
            if (succeeded)
            {
                phase_ = phase::present;
                state_ = tray_sync_state::shown;
                return std::nullopt;
            }
            if (outcome == tray_call_outcome::timed_out)
            {
                // 셸이 바빠 늦었을 뿐 아이콘은 섰을 수 있다. 방송이 오지 않으므로 같은 칸으로 바꿔 보고 섰는지 가린다.
                phase_ = phase::probing;
                tray_call probe { call };
                probe.kind = tray_call_kind::modify;
                return probe;
            }
            phase_ = phase::waiting_restart;
            state_ = tray_sync_state::waiting;
            return std::nullopt;
        case tray_call_kind::modify:
            if (phase_ == phase::probing)
            {
                phase_ = succeeded ? phase::present : phase::waiting_change;
                state_ = succeeded ? tray_sync_state::shown : tray_sync_state::waiting;
                return std::nullopt;
            }
            state_ = succeeded ? tray_sync_state::shown : tray_sync_state::failed;
            return std::nullopt;
        case tray_call_kind::remove:
            break;
        }
        state_ = succeeded ? tray_sync_state::off : tray_sync_state::failed;
        return std::nullopt;
    }

    void tray_sync::shell_restarted() noexcept
    {
        phase_ = phase::absent;
        applied_image_ = {};
        applied_size_ = 0;
        applied_text_.clear();
    }

    bool tray_sync::added() const noexcept
    {
        return phase_ == phase::present || phase_ == phase::waiting_change;
    }
}
