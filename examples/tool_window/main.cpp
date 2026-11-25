// luil tool window — 테두리 없는 작은 도구 창을 주 창으로 쓰는 macOS 예제다 (늘 위·투명 바탕·직접 그린 머릿줄).
//
// macOS host의 창 설정과 popup 판을 한자리에서 본다 (docs/concepts/build-system.md의 macOS 표).
//  - 주 창은 `borderless`·`layered`·둥근 모서리다. 머릿줄은 `caption_element`이고 앱 단추(늘 위 고정)를 담는다.
//  - "Details"는 창 오른쪽 곁에, "List"는 창 아래로 펼쳐지는 popup이다. 둘 다 창 밖에 선다.
//  - "Hover me"에 머무르면 곁에 미리보기 카드가 선다 (`hover_dwell`). 앱이 뒤에 있어도 선다.
//  - 배치 보고·끌기 중 자리 고치기·popup 최종 자리를 stderr에 `luil tool:` 줄로 적는다. tests/macos의 입력 대본이 그 줄을 본다.
//
// 인자: --smoke-test, --titled(시스템 제목 막대), --opaque(불투명 바탕), --topmost, --all-spaces, --accessory, --active-hover,
//       --placement=x,y,w,h[,max], --position=x,y, --clamp-top=<y>(끌기 중 창 위 끝의 하한)

#include "luil/luil.h"
#include "luil/macos/macos_app.h"
#include "luil/generated/codicons.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tool {
    constexpr luil::ui_element_kind kind_layout { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_button { luil::application_element_kind(1) };
    constexpr luil::ui_element_kind kind_label { luil::application_element_kind(2) };

    struct metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 1.0f };
    };

    struct close_intent
    {
    };

    struct pin_intent
    {
    };

    struct detail_intent
    {
        bool open { false };
    };

    struct list_intent
    {
        bool open { false };
    };

    struct pick_intent
    {
        int index { 0 };
    };

    struct dwell_intent
    {
        bool open { false };
    };

    struct placed_intent
    {
        std::u8string id {};
        luil::rect_f bounds {};
    };

    void log(const std::string& line)
    {
        std::fprintf(stderr, "luil tool: %s\n", line.c_str());
    }

    [[nodiscard]] std::string text(const std::u8string& value)
    {
        return { value.begin(), value.end() };
    }

    [[nodiscard]] std::unique_ptr<luil::text_button_element> make_button(const std::u8string& owner, std::u8string label, luil::ui_action action)
    {
        auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_button, owner }, luil::text_button_config { .text = std::move(label) }) };
        button->set_action(luil::ui_trigger::left_click, std::move(action));
        return button;
    }

    [[nodiscard]] std::unique_ptr<luil::label_element> make_label(const std::u8string& owner, std::u8string value)
    {
        luil::label_config config {};
        config.text = std::move(value);
        config.font_size = 13.0f;
        config.color = luil::label_color_role::primary;
        return std::make_unique<luil::label_element>(luil::ui_element_id { kind_label, owner }, config);
    }

    // popup 판의 tree다. 바탕은 표면이 칠하고 (둥근 판은 시스템이 잘라 낸다), 안에 글 줄을 쌓는다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_popup_tree(const float width, const float height, const float scale, std::unique_ptr<luil::stack_element> column)
    {
        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, width * scale, height * scale }, scale });
        column->arrange({ { 0.0f, 0.0f, width * scale, height * scale }, scale });
        root->add(std::move(column));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    class tool_driver final : public luil::logic_driver
    {
    public:
        explicit tool_driver(const bool pinned)
            : pinned_ { pinned }
        {
        }

        void handle(luil::app_message message) override
        {
            if (message.get<close_intent>() != nullptr)
            {
                closed_.store(true);
                return;
            }
            if (const auto* const metrics { message.get<metrics_intent>() }; metrics != nullptr)
                metrics_ = *metrics;
            else if (message.get<pin_intent>() != nullptr)
            {
                pinned_ = pinned_ == false;
                log(std::string { "pinned " } + (pinned_ ? "1" : "0"));
            }
            else if (const auto* const detail { message.get<detail_intent>() }; detail != nullptr)
                detail_ = detail->open;
            else if (const auto* const list { message.get<list_intent>() }; list != nullptr)
                list_ = list->open;
            else if (const auto* const dwell { message.get<dwell_intent>() }; dwell != nullptr)
            {
                dwell_ = dwell->open;
                log(std::string { "dwell " } + (dwell_ ? "1" : "0"));
            }
            else if (const auto* const pick { message.get<pick_intent>() }; pick != nullptr)
            {
                list_ = false;
                log("picked " + std::to_string(pick->index));
            }
            else if (const auto* const placed { message.get<placed_intent>() }; placed != nullptr)
                log("placed " + text(placed->id) + " " + std::to_string(static_cast<int>(placed->bounds.x)) + " " + std::to_string(static_cast<int>(placed->bounds.y)) + " "
                    + std::to_string(static_cast<int>(placed->bounds.width)) + " " + std::to_string(static_cast<int>(placed->bounds.height)));
        }

        [[nodiscard]] std::shared_ptr<const luil::ui_frame> make_frame() override
        {
            const float scale { metrics_.scale > 0.0f ? metrics_.scale : 1.0f };
            const float width { metrics_.width > 0.0f ? metrics_.width : 320.0f * scale };
            const float height { metrics_.height > 0.0f ? metrics_.height : 200.0f * scale };

            auto root { std::make_unique<luil::root_element>() };
            root->arrange({ { 0.0f, 0.0f, width, height }, scale });

            // 투명 바탕의 창이라 판을 tree가 칠한다. 모서리는 창(`corner_radius`)이 같은 반지름으로 자른다.
            luil::panel_config background_config {};
            background_config.background = [](const luil::ui_color_palette& palette) { return palette.window_background; };
            background_config.corner_radius = 10.0f;
            auto background { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_layout, u8"background" }, background_config) };
            background->arrange({ { 0.0f, 0.0f, width, height }, scale });
            root->add(std::move(background));

            // 머릿줄이다. 제목 막대가 있는 창에서는 자리를 차지하지 않고, 앱 단추는 시스템 제목 막대에 선다.
            luil::caption_config caption_config {};
            caption_config.title = u8"tool";
            caption_config.minimize_tooltip = u8"Minimize";
            caption_config.maximize_tooltip = u8"Zoom";
            caption_config.close_tooltip = u8"Close";
            caption_config.app_buttons.push_back(luil::caption_app_button {
                .key = u8"pin",
                .glyph = pinned_ ? luil::codicons::icon_pinned : luil::codicons::icon_pin,
                .tooltip = pinned_ ? u8"Unpin" : u8"Keep on top",
                .action = luil::make_message_action(pin_intent {}),
            });
            const float caption_height { luil::caption_element::height_for(caption_config) * scale };
            auto caption { std::make_unique<luil::caption_element>(caption_config) };
            caption->arrange({ { 0.0f, 0.0f, width, caption_height }, scale });
            root->add(std::move(caption));

            luil::stack_config column_config {};
            column_config.padding = luil::edge_insets::all(12.0f);
            column_config.spacing = 8.0f;
            auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"column" }, column_config) };
            column->add(make_button(u8"details", u8"Details", luil::make_message_action(detail_intent { detail_ == false })), { .length = 28.0f, .cross_length = 140.0f });
            column->add(make_button(u8"list", u8"List", luil::make_message_action(list_intent { list_ == false })), { .length = 28.0f, .cross_length = 140.0f });
            auto hover { make_button(u8"hover", u8"Hover me", luil::make_message_action(dwell_intent { false })) };
            hover->set_hover_dwell(luil::hover_dwell_target {
                .delay = std::chrono::milliseconds { 300 },
                .on_dwell = luil::make_message_action(dwell_intent { true }),
                .on_end = luil::make_message_action(dwell_intent { false }),
            });
            column->add(std::move(hover), { .length = 28.0f, .cross_length = 140.0f });
            column->add(make_label(u8"status", pinned_ ? u8"on top" : u8"normal level"), 18.0f);
            column->arrange({ { 0.0f, caption_height, width, height - caption_height }, scale });
            root->add(std::move(column));

            auto frame { std::make_shared<luil::ui_frame>() };
            frame->tree = std::make_shared<const luil::ui_tree>(std::move(root));
            frame->topmost = pinned_;
            const float logical_width { width / scale };
            const float logical_height { height / scale };
            const float logical_caption { caption_height / scale };

            // 창 오른쪽 곁의 세부 정보 카드다. 머무름으로도 선다.
            if (detail_ || dwell_)
            {
                luil::ui_popup popup {};
                popup.id = u8"detail";
                popup.x = logical_width + 8.0f;
                popup.y = logical_caption;
                popup.width = 220.0f;
                popup.height = 140.0f;
                auto lines { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"detail-lines" }, column_config) };
                lines->add(make_label(u8"detail-title", dwell_ ? u8"Preview" : u8"Details"), 18.0f);
                lines->add(make_label(u8"detail-body", u8"This card stands beside the window."), 18.0f);
                popup.tree = make_popup_tree(popup.width, popup.height, scale, std::move(lines));
                const bool from_dwell { dwell_ && detail_ == false };
                popup.dismiss = [from_dwell](const luil::popup_dismiss_reason reason) -> luil::input_action {
                    if (from_dwell)
                        return luil::app_message {};
                    if (reason == luil::popup_dismiss_reason::surface_moved || reason == luil::popup_dismiss_reason::surface_resized)
                        return luil::app_message {};
                    return luil::app_message { detail_intent { false } };
                };
                popup.placed = [](const luil::rect_f& bounds) { return luil::app_message { placed_intent { u8"detail", bounds } }; };
                frame->popups.push_back(std::move(popup));
            }
            // 작은 창 밖으로 펼쳐지는 목록이다.
            if (list_)
            {
                luil::ui_popup popup {};
                popup.id = u8"list";
                popup.x = 12.0f;
                popup.y = logical_height - 24.0f;
                popup.width = 200.0f;
                popup.height = 8.0f * 32.0f + 24.0f;
                auto rows { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"list-rows" }, column_config) };
                for (int index { 0 }; index < 8; ++index)
                {
                    const std::string digits { std::to_string(index) };
                    const std::u8string number { digits.begin(), digits.end() };
                    rows->add(make_button(u8"row-" + number, u8"Item " + number, luil::make_message_action(pick_intent { index })), 24.0f);
                }
                popup.tree = make_popup_tree(popup.width, popup.height, scale, std::move(rows));
                popup.dismiss = [](const luil::popup_dismiss_reason) -> luil::input_action { return luil::app_message { list_intent { false } }; };
                popup.placed = [](const luil::rect_f& bounds) { return luil::app_message { placed_intent { u8"list", bounds } }; };
                frame->popups.push_back(std::move(popup));
            }
            return frame;
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { close_intent {} };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed_.load();
        }

    private:
        metrics_intent metrics_ {};
        bool pinned_ { false };
        bool detail_ { false };
        bool list_ { false };
        bool dwell_ { false };
        std::atomic<bool> closed_ { false };
    };

    // UI thread의 훅이다. 배치 보고와 끌기 중의 자리를 적고, 끌기 중 창 위 끝을 하한 아래로 내리지 않는다.
    class tool_delegate final : public luil::win32::window_delegate
    {
    public:
        explicit tool_delegate(const std::optional<int> clamp_top)
            : clamp_top_ { clamp_top }
        {
        }

        [[nodiscard]] luil::app_message make_window_metrics_message(const float width, const float height, const float scale) override
        {
            return luil::app_message { metrics_intent { width, height, scale } };
        }

        [[nodiscard]] luil::app_message make_window_placement_message(const luil::window_placement& placement) override
        {
            log("placement " + std::to_string(placement.x) + " " + std::to_string(placement.y) + " " + std::to_string(placement.width) + " " + std::to_string(placement.height) + " max="
                + (placement.maximized ? "1" : "0") + " full=" + (placement.fullscreen ? "1" : "0"));
            return {};
        }

        void on_window_moving(luil::app_host& host, luil::screen_rect& bounds) override
        {
            static_cast<void>(host);
            if (clamp_top_.has_value() && bounds.top < *clamp_top_)
            {
                const int height { bounds.height() };
                bounds.top = *clamp_top_;
                bounds.bottom = bounds.top + height;
            }
            log("moving " + std::to_string(bounds.left) + " " + std::to_string(bounds.top));
        }

    private:
        std::optional<int> clamp_top_ {};
    };

    // "x,y,w,h[,max]"다. 숫자가 아니면 빈 값이다.
    [[nodiscard]] std::optional<luil::window_placement> parse_placement(const std::u8string_view value)
    {
        std::vector<int> numbers {};
        std::string current {};
        bool maximized { false };
        const std::string all { value.begin(), value.end() };
        for (std::size_t start { 0 }; start <= all.size();)
        {
            const std::size_t comma { std::min(all.find(',', start), all.size()) };
            const std::string part { all.substr(start, comma - start) };
            if (part == "max")
                maximized = true;
            else
            {
                char* end { nullptr };
                const long number { std::strtol(part.c_str(), &end, 10) };
                if (part.empty() || end == nullptr || *end != '\0')
                    return std::nullopt;
                numbers.push_back(static_cast<int>(number));
            }
            start = comma + 1;
        }
        if (numbers.size() != 4)
            return std::nullopt;
        return luil::window_placement { numbers[0], numbers[1], numbers[2], numbers[3], maximized, false };
    }
}

int main(const int argc, const char* const* const argv)
{
    luil::macos::application_config config {};
    config.title = u8"luil tool";
    config.initial_width = 320;
    config.initial_height = 200;
    config.minimum_width = 240;
    config.minimum_height = 160;
    config.borderless = true;
    config.presentation = luil::win32::window_presentation::layered;
    config.corner_radius = 10.0f;
    std::optional<int> clamp_top {};
    for (const std::u8string& argument : luil::macos::command_line_arguments(argc, argv))
    {
        if (argument == u8"--smoke-test")
            config.smoke_test = true;
        else if (argument == u8"--titled")
        {
            config.borderless = false;
            config.corner_radius = 0.0f;
            config.presentation = luil::win32::window_presentation::standard;
        }
        else if (argument == u8"--opaque")
            config.presentation = luil::win32::window_presentation::standard;
        else if (argument == u8"--topmost")
            config.topmost = true;
        else if (argument == u8"--all-spaces")
            config.all_spaces = true;
        else if (argument == u8"--accessory")
            config.activation = luil::macos::activation_policy::accessory;
        else if (argument == u8"--active-hover")
            config.inactive_hover = false;
        else if (argument.starts_with(u8"--placement="))
            config.initial_placement = tool::parse_placement(std::u8string_view { argument }.substr(12));
        else if (argument.starts_with(u8"--position="))
            config.initial_position = luil::win32::parse_window_position(std::u8string_view { argument }.substr(11));
        else if (argument.starts_with(u8"--clamp-top="))
            clamp_top = std::atoi(reinterpret_cast<const char*>(argument.c_str()) + 12);
    }

    tool::tool_driver driver { config.topmost };
    tool::tool_delegate delegate { clamp_top };
    luil::macos::application_environment environment {};
    environment.driver = &driver;
    environment.delegate = &delegate;
    return luil::macos::run_application(config, environment);
}
