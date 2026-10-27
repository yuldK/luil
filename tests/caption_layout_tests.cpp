#include "luil/ui/caption_element.h"
#include "luil/ui/caption_metrics.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/root_element.h"
#include "luil/ui/ui_tree.h"
#include "win32/caption_layout.h"
#include "win32/caption_surface.h"
#include "win32/window_mode.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

TEST_CASE("Caption button bounds scale with DPI", "[caption]")
{
    const auto layout = luil::win32::make_caption_layout(1000, 96);
    REQUIRE(layout.height == luil::default_caption_ui_metrics.height);
    REQUIRE(layout.button_width == luil::default_caption_ui_metrics.button_width);
    REQUIRE(layout.close_left == 958);
    REQUIRE(layout.maximize_left == 916);
    REQUIRE(layout.minimize_left == 874);

    const auto scaled = luil::win32::make_caption_layout(1500, 144);
    REQUIRE(scaled.height == 54);
    REQUIRE(scaled.button_width == 63);
}

TEST_CASE("Caption hit testing distinguishes drag and system buttons", "[caption]")
{
    const auto layout = luil::win32::make_caption_layout(1000, 96);
    REQUIRE(luil::win32::hit_test_caption(layout, 100, 20) == luil::win32::caption_hit::drag);
    REQUIRE(luil::win32::hit_test_caption(layout, 880, 20) == luil::win32::caption_hit::minimize);
    REQUIRE(luil::win32::hit_test_caption(layout, 930, 20) == luil::win32::caption_hit::maximize);
    REQUIRE(luil::win32::hit_test_caption(layout, 980, 20) == luil::win32::caption_hit::close);
    REQUIRE(luil::win32::hit_test_caption(layout, 100, 60) == luil::win32::caption_hit::client);
}

TEST_CASE("A caption without some buttons pulls the rest to the right edge", "[caption]")
{
    // 최소화·최대화를 뺀 도구 창 캡션이다.
    // 빠진 버튼은 자리를 차지하지 않고 닫기가 오른쪽 끝에 남는다.
    constexpr luil::caption_buttons close_only { .minimize = false, .maximize = false, .close = true };
    const auto layout = luil::win32::make_caption_layout(1000, 96, luil::default_caption_ui_metrics, close_only);
    REQUIRE(layout.close_left == 958);
    // 없는 버튼의 왼쪽은 그 시점의 오른쪽 끝이라 어떤 x로도 맞지 않는다.
    REQUIRE(layout.maximize_left == layout.close_left);
    REQUIRE(layout.minimize_left == layout.close_left);

    // 판정은 한 줄도 바뀌지 않았는데 결과가 맞는다.
    REQUIRE(luil::win32::hit_test_caption(layout, 980, 20) == luil::win32::caption_hit::close);
    REQUIRE(luil::win32::hit_test_caption(layout, 930, 20) == luil::win32::caption_hit::drag);
    REQUIRE(luil::win32::hit_test_caption(layout, 880, 20) == luil::win32::caption_hit::drag);
}

TEST_CASE("A caption without the close button keeps the others in order", "[caption]")
{
    // 가운데를 빼는 경우도 순서가 유지되어야 한다.
    constexpr luil::caption_buttons no_maximize { .minimize = true, .maximize = false, .close = true };
    const auto layout = luil::win32::make_caption_layout(1000, 96, luil::default_caption_ui_metrics, no_maximize);
    REQUIRE(layout.close_left == 958);
    REQUIRE(layout.maximize_left == 958);
    REQUIRE(layout.minimize_left == 916);
    REQUIRE(luil::win32::hit_test_caption(layout, 980, 20) == luil::win32::caption_hit::close);
    REQUIRE(luil::win32::hit_test_caption(layout, 930, 20) == luil::win32::caption_hit::minimize);
    REQUIRE(luil::win32::hit_test_caption(layout, 880, 20) == luil::win32::caption_hit::drag);

    // 버튼이 하나도 없으면 캡션 전체가 끌기다.
    constexpr luil::caption_buttons none { .minimize = false, .maximize = false, .close = false };
    const auto bare = luil::win32::make_caption_layout(1000, 96, luil::default_caption_ui_metrics, none);
    REQUIRE(luil::win32::hit_test_caption(bare, 980, 20) == luil::win32::caption_hit::drag);
}

TEST_CASE("The caption element builds only the configured buttons", "[caption]")
{
    // 그리기 쪽 자리가 hit test 쪽과 같아야 한다 — 두 계산이 같은 집합을 본다.
    luil::caption_config config {};
    config.buttons = { .minimize = false, .maximize = false, .close = true };
    auto tree = luil::make_caption_tree(1000.0f, 1.0f, config);

    REQUIRE(tree.find(luil::ui_element_id { luil::ui_element_kind::caption_minimize }) == nullptr);
    REQUIRE(tree.find(luil::ui_element_id { luil::ui_element_kind::caption_maximize }) == nullptr);
    const luil::ui_element* const close = tree.find(luil::ui_element_id { luil::ui_element_kind::caption_close });
    REQUIRE(close != nullptr);

    const auto layout = luil::win32::make_caption_layout(1000, 96, config.metrics, config.buttons);
    REQUIRE(static_cast<int>(close->bounds().x) == layout.close_left);

    // 기본 설정에서는 셋 다 있고 자리도 그대로다.
    auto full = luil::make_caption_tree(1000.0f, 1.0f, luil::caption_config {});
    const luil::ui_element* const minimize = full.find(luil::ui_element_id { luil::ui_element_kind::caption_minimize });
    REQUIRE(minimize != nullptr);
    REQUIRE(static_cast<int>(minimize->bounds().x) == luil::win32::make_caption_layout(1000, 96).minimize_left);
}

TEST_CASE("A missing caption button drops its window style", "[caption]")
{
    // 버튼을 빼면 캡션 더블클릭·Win+↑·시스템 메뉴까지 함께 멎어야 한다.
    constexpr luil::caption_buttons close_only { .minimize = false, .maximize = false, .close = true };
    const DWORD style = luil::win32::window_style_for(close_only, luil::win32::window_display_mode::normal);
    REQUIRE((style & WS_MAXIMIZEBOX) == 0);
    REQUIRE((style & WS_MINIMIZEBOX) == 0);
    // 크기 조절과 시스템 메뉴는 버튼과 무관하다.
    REQUIRE((style & luil::win32::retained_window_styles) == luil::win32::retained_window_styles);
    // 만들 때와 크기를 잴 때 쓰는 스타일은 시스템 캡션 하나만 다르다.
    REQUIRE(luil::win32::custom_window_style_for(close_only, luil::win32::window_display_mode::normal) == (style & ~static_cast<DWORD>(WS_CAPTION)));
}

TEST_CASE("The caption geometry is the same computation the window hit test uses", "[caption][window-mode]")
{
    // 캡션 자리를 두 번 재지 않는다는 계약이다.
    // 비클라이언트 판정은 `make_caption_layout`이 낸 자리를 그대로 쓰므로,
    // 버튼을 뺀 캡션의 빈자리는 판정에서도 그대로 끌기가 된다.
    constexpr luil::caption_buttons close_only { .minimize = false, .maximize = false, .close = true };
    const luil::win32::window_frame_metrics metrics { luil::default_caption_ui_metrics, close_only, 4, 10 };
    const auto layout = luil::win32::make_caption_layout(1000, 96, luil::default_caption_ui_metrics, close_only);

    // 닫기 버튼의 왼쪽 경계 바로 오른쪽은 두 계산 모두에서 닫기다.
    REQUIRE(luil::win32::hit_test_caption(layout, layout.close_left + 1, 20) == luil::win32::caption_hit::close);
    REQUIRE(luil::win32::hit_test_window(metrics, luil::win32::window_display_mode::normal, 1000, 800, 96, layout.close_left + 1, 20) == luil::win32::window_hit::close_button);
    // 빠진 최대화 버튼의 자리는 끌기다.
    REQUIRE(luil::win32::hit_test_window(metrics, luil::win32::window_display_mode::normal, 1000, 800, 96, 930, 20) == luil::win32::window_hit::caption_drag);
}

namespace {
    [[nodiscard]] luil::caption_app_button make_app_button(const char8_t* key, const bool enabled = true)
    {
        return luil::caption_app_button {
            .key = key,
            .glyph = 0xEB51,
            .tooltip = key,
            .action = [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; },
            .enabled = enabled,
        };
    }
} // namespace

TEST_CASE("Caption app buttons stand left of the window buttons in their listed order", "[caption]")
{
    luil::caption_config config {};
    config.app_buttons = { make_app_button(u8"search"), make_app_button(u8"settings") };
    // 액션이 없는 단추는 만들지 않는다.
    config.app_buttons.push_back(luil::caption_app_button { .key = u8"inert" });
    auto tree = luil::make_caption_tree(1000.0f, 1.0f, config);

    const auto layout = luil::win32::make_caption_layout(1000, 96, config.metrics, config.buttons);
    const luil::ui_element* const search = tree.find(luil::ui_element_id { luil::ui_element_kind::caption_app_button, u8"search" });
    const luil::ui_element* const settings = tree.find(luil::ui_element_id { luil::ui_element_kind::caption_app_button, u8"settings" });
    REQUIRE(search != nullptr);
    REQUIRE(settings != nullptr);
    REQUIRE(tree.find(luil::ui_element_id { luil::ui_element_kind::caption_app_button, u8"inert" }) == nullptr);
    // 창 버튼 바로 왼쪽부터 목록의 끝이 오른쪽이다. 창 버튼의 자리는 그대로다.
    REQUIRE(static_cast<int>(settings->bounds().x) == layout.minimize_left - layout.button_width);
    REQUIRE(static_cast<int>(search->bounds().x) == layout.minimize_left - 2 * layout.button_width);
    const luil::ui_element* const minimize = tree.find(luil::ui_element_id { luil::ui_element_kind::caption_minimize });
    REQUIRE(static_cast<int>(minimize->bounds().x) == layout.minimize_left);

    // 보조 기술이 형제를 읽는 차례가 화면의 차례다 — 앱 단추가 창 버튼보다 앞이다.
    const luil::ui_element& caption = *tree.root();
    REQUIRE(caption.children()[0]->id().owner == u8"search");
    REQUIRE(caption.children()[1]->id().owner == u8"settings");
    // 창 버튼과 달리 Tab의 자리이고 이름은 tooltip이다.
    REQUIRE(search->tab_stop());
    REQUIRE(minimize->tab_stop() == false);
    REQUIRE(search->tooltip() == u8"search");
}

TEST_CASE("The caption drag strip answers client over an app button", "[caption][window-mode]")
{
    luil::caption_config config {};
    config.app_buttons = { make_app_button(u8"settings"), make_app_button(u8"off", false) };
    const auto tree = luil::make_caption_tree(1000.0f, 1.0f, config);
    const auto layout = luil::win32::make_caption_layout(1000, 96, config.metrics, config.buttons);
    const int off_x { layout.minimize_left - layout.button_width / 2 };
    const int settings_x { layout.minimize_left - layout.button_width - layout.button_width / 2 };

    // 비클라이언트 판정 자체는 그 자리를 끌기로 본다 — 단추를 아는 것은 tree다.
    REQUIRE(luil::win32::hit_test_caption(layout, settings_x, 18) == luil::win32::caption_hit::drag);
    REQUIRE(luil::win32::caption_client_at(&tree, settings_x, 18));
    // 비활성 단추 위도 끌기 자리가 아니다.
    REQUIRE(luil::win32::caption_client_at(&tree, off_x, 18));
    // 제목 자리와 caption 아래는 단추가 아니다.
    REQUIRE(luil::win32::caption_client_at(&tree, 100, 18) == false);
    REQUIRE(luil::win32::caption_client_at(&tree, settings_x, 60) == false);
    REQUIRE(luil::win32::caption_client_at(nullptr, settings_x, 18) == false);

    // 앱 단추가 없는 caption은 지금처럼 전부 끌기다.
    const auto plain = luil::make_caption_tree(1000.0f, 1.0f, luil::caption_config {});
    REQUIRE(luil::win32::caption_client_at(&plain, settings_x, 18) == false);
}

namespace {
    // 자식을 자기 bounds 안으로 자르는 칸이다 (흘려 감춘 자리를 본다).
    class clipping_panel final : public luil::ui_element
    {
    public:
        explicit clipping_panel(const char8_t* key)
            : ui_element { luil::ui_element_id { luil::application_element_kind(2), key } }
        {
            set_clip_children(true);
        }

        void add(std::unique_ptr<luil::ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    [[nodiscard]] std::unique_ptr<luil::ui_element> make_caption_field(const char8_t* key, const luil::rect_f& slot)
    {
        auto field { std::make_unique<luil::text_button_element>(luil::ui_element_id { luil::application_element_kind(1), key }, luil::text_button_config {}) };
        field->set_caption_client(true);
        field->arrange({ slot, 1.0f });
        return field;
    }
} // namespace

TEST_CASE("Any element marked as a caption client turns the drag strip into client", "[caption][window-mode]")
{
    // 브라우저·편집기형 제목 표시줄: caption 위에 겹쳐 세운 검색 칸이 그 자리를 client로 만든다.
    luil::caption_config config {};
    auto root { std::make_unique<luil::root_element>() };
    root->arrange({ { 0.0f, 0.0f, 1000.0f, 600.0f }, 1.0f });
    auto caption { std::make_unique<luil::caption_element>(config) };
    caption->arrange({ { 0.0f, 0.0f, 1000.0f, 36.0f }, 1.0f });
    root->add(std::move(caption));
    root->add(make_caption_field(u8"search", { 400.0f, 4.0f, 200.0f, 28.0f }));

    auto disabled { make_caption_field(u8"back", { 100.0f, 4.0f, 28.0f, 28.0f }) };
    disabled->set_enabled(false);
    root->add(std::move(disabled));

    auto hidden { make_caption_field(u8"hidden", { 200.0f, 4.0f, 28.0f, 28.0f }) };
    hidden->set_visible(false);
    root->add(std::move(hidden));

    // 칸 폭 40 안으로 잘린 단추: 잘려 나간 오른쪽은 끌기 자리다.
    auto strip { std::make_unique<clipping_panel>(u8"strip") };
    strip->arrange({ { 700.0f, 4.0f, 40.0f, 28.0f }, 1.0f });
    strip->add(make_caption_field(u8"clipped", { 700.0f, 4.0f, 120.0f, 28.0f }));
    root->add(std::move(strip));

    const luil::ui_tree tree { std::move(root) };
    const auto layout = luil::win32::make_caption_layout(1000, 96, config.metrics, config.buttons);
    // 비클라이언트 판정 자체는 그 자리를 끌기로 본다 — 칸을 아는 것은 tree다.
    REQUIRE(luil::win32::hit_test_caption(layout, 500, 18) == luil::win32::caption_hit::drag);
    REQUIRE(luil::win32::caption_client_at(&tree, 500, 18));
    // 칸 옆은 그대로 끌기다.
    REQUIRE(luil::win32::caption_client_at(&tree, 380, 18) == false);
    // 비활성 칸도 끌기 자리가 아니다.
    REQUIRE(luil::win32::caption_client_at(&tree, 110, 18));
    // 숨긴 칸은 없는 것이다.
    REQUIRE(luil::win32::caption_client_at(&tree, 210, 18) == false);
    // 잘린 칸은 보이는 자리만 client다.
    REQUIRE(luil::win32::caption_client_at(&tree, 720, 18));
    REQUIRE(luil::win32::caption_client_at(&tree, 780, 18) == false);
}

TEST_CASE("A fullscreen window answers client everywhere, caption clients or not", "[caption][window-mode]")
{
    // 전체 화면에는 끌기 띠가 없다. 표시와 상관없이 모든 자리가 client다.
    const luil::win32::window_frame_metrics metrics { luil::default_caption_ui_metrics, luil::default_caption_buttons, 8, 16 };
    REQUIRE(luil::win32::hit_test_window(metrics, luil::win32::window_display_mode::fullscreen, 1000, 800, 96, 500, 18) == luil::win32::window_hit::client);
    REQUIRE(luil::win32::hit_test_window(metrics, luil::win32::window_display_mode::normal, 1000, 800, 96, 500, 18) == luil::win32::window_hit::caption_drag);
}

TEST_CASE("A caption button runs only when pressed and released on it outside fullscreen", "[caption][window-mode]")
{
    using luil::win32::window_display_mode;
    // 누른 단추에서 뗀 것만 실행한다 — OS의 캡션 단추와 같다.
    REQUIRE(luil::win32::caption_click_executes(HTCLOSE, HTCLOSE, window_display_mode::normal));
    REQUIRE(luil::win32::caption_click_executes(HTMAXBUTTON, HTMAXBUTTON, window_display_mode::maximized));
    // 다른 곳(끌기 띠·다른 단추)에서 눌러 닫기 위에서 떼면 닫지 않는다.
    REQUIRE(luil::win32::caption_click_executes(HTNOWHERE, HTCLOSE, window_display_mode::normal) == false);
    REQUIRE(luil::win32::caption_click_executes(HTMINBUTTON, HTCLOSE, window_display_mode::normal) == false);
    // 단추가 아닌 자리의 뗌은 실행이 아니다.
    REQUIRE(luil::win32::caption_click_executes(HTCAPTION, HTCAPTION, window_display_mode::normal) == false);
    // 전체 화면으로 들어가는 동안 남은 누름의 꼬리는 실행하지 않는다.
    REQUIRE(luil::win32::caption_click_executes(HTCLOSE, HTCLOSE, window_display_mode::fullscreen) == false);
}

namespace {
    // 1000x800 창의 (slot)에 caption을 세운 tree다. 앱이 캡션을 놓는 자리는 배치가 정한다.
    [[nodiscard]] luil::ui_tree make_placed_caption_tree(const luil::rect_f& slot, const luil::caption_config& config = {}, const bool visible = true)
    {
        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, 1000.0f, 800.0f }, 1.0f });
        auto caption { std::make_unique<luil::caption_element>(config) };
        caption->arrange({ slot, 1.0f });
        caption->set_visible(visible);
        root->add(std::move(caption));
        return luil::ui_tree { std::move(root) };
    }

    [[nodiscard]] luil::win32::window_hit hit_with_band(const luil::win32::caption_band& band, const int x, const int y)
    {
        return luil::win32::hit_test_window(luil::win32::window_frame_metrics {}, luil::win32::window_display_mode::normal, 1000, 800, 96, x, y, band);
    }
} // namespace

TEST_CASE("The caption band follows where the tree placed the caption", "[caption][window-mode]")
{
    using luil::win32::window_hit;

    SECTION("창 아래에 둔 caption은 아래 띠가 끌기이고 위는 client다")
    {
        const auto tree { make_placed_caption_tree({ 0.0f, 764.0f, 1000.0f, 0.0f }) };
        const luil::win32::caption_band band { luil::win32::caption_band_in(&tree, 1000, 96) };
        REQUIRE(band == luil::win32::caption_band { 0, 764, 1000, 36 });

        REQUIRE(hit_with_band(band, 300, 780) == window_hit::caption_drag);
        REQUIRE(hit_with_band(band, 20, 780) == window_hit::system_menu);
        REQUIRE(hit_with_band(band, 880, 780) == window_hit::minimize_button);
        REQUIRE(hit_with_band(band, 930, 780) == window_hit::maximize_button);
        REQUIRE(hit_with_band(band, 980, 780) == window_hit::close_button);
        // 예전 자리(맨 위)는 이제 내용이다. 위 가장자리의 크기 조절만 남는다.
        REQUIRE(hit_with_band(band, 300, 20) == window_hit::client);
        REQUIRE(hit_with_band(band, 980, 20) == window_hit::client);
        REQUIRE(hit_with_band(band, 300, 1) == window_hit::resize_top);
        // 아래 가장자리의 크기 조절이 띠보다 먼저다 — 위에 둘 때 위 가장자리와 같은 규칙이다.
        REQUIRE(hit_with_band(band, 300, 798) == window_hit::resize_bottom);
        REQUIRE(hit_with_band(band, 998, 798) == window_hit::resize_bottom_right);
    }

    SECTION("창 일부만 덮은 caption은 그 폭 안에서 버튼이 오른쪽 끝에 선다")
    {
        const auto tree { make_placed_caption_tree({ 200.0f, 300.0f, 600.0f, 0.0f }) };
        const luil::win32::caption_band band { luil::win32::caption_band_in(&tree, 1000, 96) };
        REQUIRE(band == luil::win32::caption_band { 200, 300, 600, 36 });

        // 그린 버튼과 판정의 버튼이 같은 자리다.
        const auto layout { luil::win32::make_caption_layout(band, 96) };
        const luil::ui_element* const close { tree.find(luil::ui_element_id { luil::ui_element_kind::caption_close }) };
        const luil::ui_element* const minimize { tree.find(luil::ui_element_id { luil::ui_element_kind::caption_minimize }) };
        REQUIRE(static_cast<int>(close->bounds().x) == layout.close_left);
        REQUIRE(static_cast<int>(minimize->bounds().x) == layout.minimize_left);
        REQUIRE(static_cast<int>(close->bounds().y) == layout.top);

        REQUIRE(hit_with_band(band, 790, 310) == window_hit::close_button);
        REQUIRE(hit_with_band(band, 210, 310) == window_hit::system_menu);
        REQUIRE(hit_with_band(band, 400, 310) == window_hit::caption_drag);
        // 띠의 양옆과 위아래는 내용이다.
        REQUIRE(hit_with_band(band, 100, 310) == window_hit::client);
        REQUIRE(hit_with_band(band, 900, 310) == window_hit::client);
        REQUIRE(hit_with_band(band, 400, 290) == window_hit::client);
        REQUIRE(hit_with_band(band, 400, 340) == window_hit::client);
    }

    SECTION("띠는 tree가 실은 그 frame의 자리를 따른다")
    {
        // 아래에서 위로 밀어 올리는 도중의 frame이다. 반 픽셀 자리도 그린 자리에 맞춰 반올림한다.
        const auto tree { make_placed_caption_tree({ 0.0f, 400.4f, 1000.0f, 0.0f }) };
        REQUIRE(luil::win32::caption_band_in(&tree, 1000, 96) == luil::win32::caption_band { 0, 400, 1000, 36 });
    }

    SECTION("숨긴 caption은 어느 자리도 끌기가 아니다")
    {
        const auto tree { make_placed_caption_tree({ 0.0f, 0.0f, 1000.0f, 0.0f }, {}, false) };
        const luil::win32::caption_band band { luil::win32::caption_band_in(&tree, 1000, 96) };
        REQUIRE(band == luil::win32::caption_band {});
        REQUIRE(hit_with_band(band, 300, 20) == window_hit::client);
        REQUIRE(hit_with_band(band, 980, 20) == window_hit::client);
        REQUIRE(hit_with_band(band, 20, 20) == window_hit::client);
    }

    SECTION("tree가 없거나 caption을 담지 않았으면 맨 위 띠다")
    {
        const luil::win32::caption_band top { luil::win32::default_caption_band(1000, 96) };
        REQUIRE(top == luil::win32::caption_band { 0, 0, 1000, 36 });
        REQUIRE(luil::win32::caption_band_in(nullptr, 1000, 96) == top);

        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, 1000.0f, 800.0f }, 1.0f });
        const luil::ui_tree bare { std::move(root) };
        REQUIRE(luil::win32::caption_band_in(&bare, 1000, 96) == top);
        // 기존 판정(띠를 주지 않는 호출)과 같은 답이다.
        REQUIRE(hit_with_band(top, 300, 20) == luil::win32::hit_test_window(luil::win32::window_frame_metrics {}, luil::win32::window_display_mode::normal, 1000, 800, 96, 300, 20));
    }

    SECTION("배율이 있어도 띠는 그린 자리이고 버튼 폭은 같은 식으로 잰다")
    {
        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, 1500.0f, 1200.0f }, 1.5f });
        auto caption { std::make_unique<luil::caption_element>(luil::caption_config {}) };
        caption->arrange({ { 0.0f, 1146.0f, 1500.0f, 0.0f }, 1.5f });
        root->add(std::move(caption));
        const luil::ui_tree tree { std::move(root) };

        const luil::win32::caption_band band { luil::win32::caption_band_in(&tree, 1500, 144) };
        REQUIRE(band == luil::win32::caption_band { 0, 1146, 1500, 54 });
        const auto layout { luil::win32::make_caption_layout(band, 144) };
        const luil::ui_element* const close { tree.find(luil::ui_element_id { luil::ui_element_kind::caption_close }) };
        REQUIRE(static_cast<int>(close->bounds().x) == layout.close_left);
        REQUIRE(luil::win32::hit_test_window(luil::win32::window_frame_metrics {}, luil::win32::window_display_mode::normal, 1500, 1200, 144, 1480, 1170, band)
                == window_hit::close_button);
    }
}
