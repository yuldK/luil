#include "win32/secondary_surface.h"

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/win32/app_host.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace {
    // 표면 하나를 창 없이 세우기 위한 최소 문맥이다.
    // 이 test가 묻는 것은 전부 창을 만들기 전에 정해지는 값이라 HWND가 없어도
    // 선다 — OS 쪽(스타일·제목)은 창이 붙었을 때만 도는 갈래다.
    class fake_surface_context final : public luil::win32::surface_context
    {
    public:
        [[nodiscard]] luil::win32::app_host* host() noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] const luil::win32::window_config& config() const noexcept override
        {
            return config_;
        }

        [[nodiscard]] luil::interaction_policy* policy() noexcept override
        {
            return nullptr;
        }

        // 창 없는 test는 합성하지 않는다 — Direct3D 렌더러를 세우지 않으므로
        // device를 물을 일이 없다.
        [[nodiscard]] IDCompositionDevice* composition_device(std::u8string&) noexcept override
        {
            return nullptr;
        }

        // 창 없는 test는 웹뷰를 세우지 않는다.
        [[nodiscard]] std::span<const luil::pixel_rect> apply_webviews(
            const std::u8string&, const luil::ui_tree*, IDCompositionVisual*, int, int) override
        {
            return {};
        }

        // 창 없는 test는 웹뷰를 세우지 않는다.
        [[nodiscard]] bool relay_webview_pointer(const std::u8string&, UINT, WPARAM, int, int) override
        {
            return false;
        }

        void webview_pointer_left(const std::u8string&) override
        {}
        void cancel_webview_pointer(const std::u8string&) override
        {}

        [[nodiscard]] sk_sp<SkTypeface> apply_frame_appearance(luil::frame_state&, const luil::win32::ui_frame*) override
        {
            return nullptr;
        }

        bool dismiss_popups(luil::win32::popup_dismiss_reason) override
        {
            return false;
        }

        void surface_moved(const std::u8string&) override
        {}

        void dispatch_action(luil::input_action) override
        {}

        void deliver_file_drop(const std::vector<std::u8string>&) override
        {}

        void schedule_update_repaint(const std::u8string&, const luil::interaction_snapshot&) noexcept override
        {}

        void surface_focus_lost(const std::u8string&) override
        {}

        void surface_focus_gained(const std::u8string&) override
        {}

        [[nodiscard]] const luil::win32::window_surface* anchored_popup(const std::u8string&, const std::u8string&) const noexcept override
        {
            return nullptr;
        }

        [[nodiscard]] bool pointer_capture_active() const noexcept override
        {
            return false;
        }

        void set_pointer_capture_active(bool) noexcept override
        {}

        void report_error(const std::u8string&) noexcept override
        {}

    private:
        luil::win32::window_config config_ {};
    };

    class tool_panel final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> tool_tree(const std::u8string_view owner)
    {
        auto root { std::make_unique<tool_panel>(luil::ui_element_id { luil::ui_element_kind::root, std::u8string { owner } }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 120.0f }, 1.0f });
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }
} // namespace

TEST_CASE("A secondary window adopts the caption and size limits of every frame", "[win32][window][caption]")
{
    // 캡션과 최소 크기는 창을 만들 때 한 번만 실렸다. 그 둘을 읽는 자리는 매번
    // 오는 OS 질문(비클라이언트 hit test·`WM_GETMINMAXINFO`)이라, 앱이 다음
    // frame에서 캡션을 바꾸면 그리기와 판정이 갈라섰다.
    fake_surface_context context {};
    luil::win32::secondary_surface surface { context, u8"tool" };

    luil::win32::ui_window source {};
    source.id = u8"tool";
    source.caption.title = u8"검사 결과";
    source.caption.buttons = { .minimize = true, .maximize = true, .close = true };
    source.minimum_width = 240.0f;
    source.minimum_height = 160.0f;
    source.tree = tool_tree(u8"first");

    // 창을 만들 때의 값이 실리고 첫 tree가 붙는다
    // (실물에서는 `create_secondary`가 이 순서로 한다).
    surface.set_caption(source.caption);
    surface.set_minimum_client_size(source.minimum_width, source.minimum_height);
    REQUIRE(surface.adopt(source));
    REQUIRE(surface.caption().title == u8"검사 결과");

    // 다음 frame이 캡션과 한계를 바꾼다.
    luil::win32::ui_window next { source };
    next.caption.title = u8"검사 결과 (2)";
    next.caption.buttons = { .minimize = false, .maximize = false, .close = true };
    next.caption.metrics.height = 28;
    next.minimum_width = 320.0f;
    next.minimum_height = 200.0f;
    next.tree = source.tree;

    // tree가 그대로라 다시 그릴 것은 없다 — 보이는 캡션은 앱이 tree에 담은
    // `caption_element`가 그리므로 픽셀이 같다. 그래도 값은 닿아야 한다.
    REQUIRE(surface.adopt(next) == false);
    REQUIRE(surface.caption().title == u8"검사 결과 (2)");
    REQUIRE(surface.caption().buttons.maximize == false);
    REQUIRE(surface.caption().buttons.minimize == false);
    // 비클라이언트 hit test가 이 치수로 캡션 자리를 판정한다.
    REQUIRE(surface.caption().metrics.height == 28);
    // `WM_GETMINMAXINFO`가 이 값으로 답한다.
    REQUIRE(surface.minimum_client_width() == 320.0f);
    REQUIRE(surface.minimum_client_height() == 200.0f);

    // tree가 바뀌면 다시 그리기가 필요하다고 보고한다.
    luil::win32::ui_window redrawn { next };
    redrawn.tree = tool_tree(u8"second");
    REQUIRE(surface.adopt(redrawn));
}
