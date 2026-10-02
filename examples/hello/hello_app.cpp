#include "hello/hello_app.h"

#include <string>
#include <utility>
#include <vector>

namespace hello {
    namespace {
        // 창 caption의 겉모습이다.
        // 같은 설정을 두 곳이 쓴다: 창(window_config.caption — 비클라이언트 hit test)과
        // tree의 caption_element(그리기·tooltip). 값이 같아야 버튼 자리가 맞물린다.
        [[nodiscard]] luil::caption_config make_caption()
        {
            luil::caption_config caption {};
            caption.title = u8"hello luil";
            caption.minimize_tooltip = u8"Minimize";
            caption.maximize_tooltip = u8"Maximize or restore";
            caption.close_tooltip = u8"Close";
            return caption;
        }

        [[nodiscard]] std::u8string count_text(const int clicks)
        {
            const std::string digits { std::to_string(clicks) };
            return u8"버튼을 누른 횟수: " + std::u8string { digits.begin(), digits.end() };
        }
    } // namespace

    void hello_driver::handle(luil::app_message message)
    {
        // 메시지 타입별로 상태를 바꾼다.
        // get<T>()는 타입이 맞을 때만 값을 돌려주므로 순서대로 물어보면 된다.
        if (message.get<close_intent>() != nullptr)
        {
            // 종료 저장이 있다면 여기서 제출한다. 이 예제는 저장할 것이 없다.
            closed_.store(true);
            return;
        }
        if (const auto* const metrics { message.get<metrics_intent>() }; metrics != nullptr)
        {
            metrics_ = *metrics;
            return;
        }
        if (message.get<increment_intent>() != nullptr)
        {
            ++clicks_;
            return;
        }
    }

    std::shared_ptr<const luil::win32::ui_frame> hello_driver::make_frame()
    {
        // 배치 단위 규칙: 설정·길이는 논리 픽셀(96 DPI 기준)이고,
        // arrange의 slot 좌표는 물리 픽셀이다. scale이 그 사이를 잇는다.
        const float scale { metrics_.scale > 0.0f ? metrics_.scale : 1.0f };
        const float width { metrics_.width > 0.0f ? metrics_.width : 800.0f };
        const float height { metrics_.height > 0.0f ? metrics_.height : 500.0f };

        // 1) 최상위 컨테이너. 창 배경은 렌더러가 팔레트로 칠하므로 담기만 한다.
        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, width, height }, scale });

        // 2) 맨 위 막대. 데스크톱은 custom caption, 모바일은 앱 바다.
        //    어느 쪽인지는 플랫폼이 알려 준다. 높이는 height_for가 알려 준다 (논리 픽셀 → 배율 곱).
        const luil::caption_config caption_config { make_caption() };
        float caption_height { 0.0f };
        if (luil::current_ui_platform().window_caption)
        {
            caption_height = luil::caption_element::height_for(caption_config) * scale;
            auto caption { std::make_unique<luil::caption_element>(caption_config) };
            caption->arrange({ { 0.0f, 0.0f, width, caption_height }, scale });
            root->add(std::move(caption));
        }
        else
        {
            const luil::app_bar_config app_bar_config { .title = caption_config.title };
            caption_height = luil::app_bar_element::height_for(app_bar_config) * scale;
            auto app_bar { std::make_unique<luil::app_bar_element>(app_bar_config) };
            app_bar->arrange({ { 0.0f, 0.0f, width, caption_height }, scale });
            root->add(std::move(app_bar));
        }

        // 3) 내용: 세로 stack에 라벨과 버튼을 쌓는다.
        //    stack의 길이 인자는 논리 픽셀이고 배율은 stack의 arrange가 곱한다.
        luil::stack_config column_config {};
        column_config.padding = luil::edge_insets::all(24.0f);
        column_config.spacing = 12.0f;
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_label, u8"layout" }, column_config) };

        luil::label_config label_config {};
        label_config.text = count_text(clicks_);
        label_config.font_size = 14.0f;
        label_config.color = luil::label_color_role::primary;
        auto label { std::make_unique<luil::label_element>(luil::ui_element_id { kind_label }, label_config) };
        column->add(std::move(label), luil::label_element::height_for(label_config));

        // 버튼의 액션은 상태를 바꾸지 않는다.
        // increment_intent 메시지를 만들 뿐이고, 실제 증가는 handle이 한다.
        // 이 규칙 덕에 tree가 불변이어도 안전하다.
        auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_button }, luil::text_button_config { .text = u8"하나 더" }) };
        button->set_cursor(luil::ui_cursor::hand);
        button->set_action(luil::ui_trigger::left_click, luil::make_message_action(increment_intent {}));
        column->add(std::move(button), { .length = 30.0f, .cross_length = 120.0f });

        // caption 아래 나머지 영역이 내용의 slot이다.
        column->arrange({ { 0.0f, caption_height, width, height - caption_height }, scale });
        root->add(std::move(column));

        // 4) frame으로 묶어 게시한다.
        //    frame은 화면의 전부다: tree, (있다면) popup·보조 창, 외양 선호.
        auto frame { std::make_shared<luil::win32::ui_frame>() };
        frame->tree = std::make_shared<const luil::ui_tree>(std::move(root));
        return frame;
    }

    luil::app_message hello_driver::make_close_message()
    {
        return luil::app_message { close_intent {} };
    }

    bool hello_driver::shutdown_completed() const
    {
        return closed_.load();
    }

    luil::app_message hello_delegate::make_window_metrics_message(const float width, const float height, const float scale)
    {
        // 창 크기가 바뀔 때마다 이 메시지가 오고, driver가 새 크기로 다시 배치한다.
        return luil::app_message { metrics_intent { width, height, scale } };
    }
} // namespace hello
