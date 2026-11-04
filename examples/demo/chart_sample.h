#pragma once

// 데스크톱과 모바일 데모의 차트 페이지가 함께 쓰는 견본이다 (docs/chart-design.md의 "확인").
//  - 1월~12월에 선 계열 "매출"과 막대 계열 "비용"을 싣는다. 종류 고르기는 "매출"의 꼴을 바꾸고, 큰 자료를 켜면 두 계열이 10만 점이 된다.
//  - 고른 점은 앱 상태다. 차트는 메시지만 내고 이 견본이 받아 다음 tree에 넘긴다.
//
// **이 헤더는 차트 헤더를 들이지 않는다.** 모바일 페이지 목록이 페이지 키를 얻으려고 페이지 헤더를 들이고, 그 소스는 차트를 링크하지 않는
// core test에도 컴파일된다. 차트 형은 .cpp에만 둔다.

#include "luil/ui/app_message.h"
#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/ui_element.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace demo {
    // "매출" 계열의 꼴이다.
    enum class chart_sample_kind
    {
        line,
        bar,
        area,
    };

    struct chart_sample_point
    {
        std::size_t series { 0 };
        std::size_t category { 0 };

        [[nodiscard]] bool operator==(const chart_sample_point&) const = default;
    };

    // --- 이 견본의 메시지 ---
    // 차트가 이 점을 고르라고 했다 (누름, 끌기, 키).
    struct chart_sample_point_intent
    {
        chart_sample_point point {};
    };

    struct chart_sample_kind_intent
    {
        chart_sample_kind kind { chart_sample_kind::line };
    };

    // 큰 자료를 켜거나 끈다. 누른 때의 반대 값을 절대 값으로 담아 거듭 와도 결과가 같다.
    struct chart_sample_large_intent
    {
        bool on { false };
    };

    class chart_sample
    {
    public:
        // 지은 차트와, 화면과 보조 기술이 같은 말을 하게 그 글을 옮겨 적은 두 label의 글이다.
        struct built
        {
            std::unique_ptr<luil::ui_element> chart {};
            // "고른 점: 매출, 3월, 120 · 계열 2 · 범주 12", 없으면 "고른 점: 없음"이다.
            std::u8string state {};
            // 차트 root의 접근 요약 그대로다.
            std::u8string summary {};
        };

        chart_sample();

        bool handle(const luil::app_message& message);
        [[nodiscard]] built build_chart(std::u8string owner) const;
        [[nodiscard]] luil::choice_group_config kind_choice(std::u8string owner) const;
        [[nodiscard]] luil::check_config large_switch(std::u8string owner) const;

    private:
        chart_sample_kind kind_ { chart_sample_kind::line };
        bool large_ { false };
        std::optional<chart_sample_point> chosen_ {};
        std::shared_ptr<const std::vector<double>> sales_ {};
        std::shared_ptr<const std::vector<double>> costs_ {};
        // 큰 자료는 처음 켤 때 한 번 짓는다.
        std::shared_ptr<const std::vector<double>> large_sales_ {};
        std::shared_ptr<const std::vector<double>> large_costs_ {};
    };
}
