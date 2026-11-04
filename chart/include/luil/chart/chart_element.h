#pragma once

#include "luil/chart/chart_scale.h"
#include "luil/ui/ui_element.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace luil {
    // 계열을 그리는 꼴이다.
    enum class chart_series_kind
    {
        line,
        // 범주 칸의 70%를 막대 계열 수로 나눠 나란히 세운다. 쌓지 않는다.
        bar,
        // 계열색을 알파 0.25로 0까지 채운 뒤 같은 선을 긋는다.
        area,
    };

    // 점에 그리는 표식이다.
    enum class chart_marker
    {
        // 고대비와 고른 점에서만 그린다. 모양은 계열 차례(원, 네모, 세모, 마름모)다.
        automatic,
        none,
        circle,
        square,
        triangle,
        diamond,
    };

    struct chart_series
    {
        std::u8string name {};
        chart_series_kind kind { chart_series_kind::line };
        // 범주 하나에 값 하나다. NaN과 무한은 빈 점이라 선과 영역은 끊고 막대는 그리지 않는다. 범주보다 짧으면 뒤가 빈 점이다.
        //  - 게시된 tree는 불변이라 frame마다 복사하지 않고 나눠 쥔다.
        std::shared_ptr<const std::vector<double>> values {};
        // 비면 계열 표의 차례 색이다. 고대비에서는 이 색도 `primary_foreground`로 접힌다.
        std::optional<ui_color> color {};
        chart_marker marker { chart_marker::automatic };
    };

    // 계열과 범주로 가리키는 점 하나다.
    struct chart_point
    {
        std::size_t series { 0 };
        std::size_t category { 0 };

        [[nodiscard]] bool operator==(const chart_point&) const = default;
    };

    // 범례 줄의 자리다.
    enum class chart_legend
    {
        bottom,
        top,
        none,
    };

    struct chart_config
    {
        // 차트, 플롯, 칸의 owner다.
        std::u8string owner {};
        // 접근 이름이다. 비면 기본 문구(차트)다.
        std::u8string name {};
        // x 이름표다. 모자라면 1부터의 번호다. 범주 수는 이것과 가장 긴 계열 가운데 큰 쪽이다.
        std::vector<std::u8string> categories {};
        std::vector<chart_series> series {};
        // 비면 빈 점을 뺀 자료의 최소·최대이고, 막대·영역 계열이 있으면 0을 넣는다. 축은 이 범위를 덮는 둥근 눈금이다.
        std::optional<chart_range> y_range {};
        chart_legend legend { chart_legend::bottom };
        bool grid { true };
        // 고른 점이다 (앱 상태). 범위 밖이면 고르지 않은 것으로 본다.
        std::optional<chart_point> focused {};
        // 이 점을 고르라는 절대 메시지다. 누름, 끌기, 키가 모두 이 하나를 탄다.
        //  - 비면 고를 수 없는 그림이다. Tab 자리와 키가 없고 접근 노드는 root 하나이며, 칸은 hover만 맡는다.
        std::function<input_action(chart_point)> focus {};
        // 값의 글이다. 눈금, 값 상자, 접근 글이 모두 쓴다. 비면 고정 소수 표기다 (눈금은 `chart_ticks::decimals` 자리,
        // 값 상자와 접근 글은 거기에 한 자리를 더한다). 필요한 소수 자리가 20을 넘으면 과학적 표기다.
        //  - 천 단위 구분, 큰 수 접미, 지역 소수점은 라이브러리에 없어 앱이 이 함수로 준다.
        //  - 배치, 그리기, 보조 기술의 질의에서 불린다. 앱 상태를 읽지 않는 함수로 준다 (게시된 tree는 불변이고 thread가 갈린다).
        std::function<std::u8string(double value)> format_value {};
        // 접근 요약이다. 비거나 빈 글을 돌려주면 기본 요약("계열 2개, 범주 12개, 최솟값 3.0, 최댓값 120.0")이다.
        std::function<std::u8string(const chart_config&)> access_summary {};
        // 고른 점의 접근 글이다. 비거나 빈 글을 돌려주면 기본 글("매출, 3월, 120.0", 계열이 하나면 이름 없이, 빈 점은 "값 없음")이다.
        std::function<std::u8string(const chart_config&, chart_point)> access_point {};
    };

    // 계열 값을 선·막대·영역으로 그리고, 같은 간격의 범주 x축과 왼쪽 y축의 눈금·격자, 범례를 붙이는 요소다 (docs/chart-design.md).
    //
    // root(`ui_element_kind::chart`)가 그림 전부를 그리고 접근 역할이 image다. 자식은 요소가 스스로 짓는다.
    //  - 플롯(`chart_plot`)은 Tab 자리와 키를 맡고, 접근 역할이 범위 없는 slider이며 값이 고른 점의 글이다.
    //  - 칸(`chart_cell`)은 플롯 아래에 서서 그리지 않고 hit, hover, 누름, 끌기만 맡는다. 플롯 폭이 있는 첫 `arrange`에서 한 번만
    //    짓는다. 수는 범주 수와 "플롯 폭(논리 픽셀) / 4" 가운데 작은 값이라 점이 많아도 tree는 플롯 폭만큼만 큰다.
    //
    // 높이는 앱이 준다 (목록 행처럼 담는 쪽의 길이를 쓴다). 자료 움직임은 없다. 값 상자는 popup이 아니라 요소가 자기 안에 그린다.
    class chart_element final : public ui_element
    {
    public:
        explicit chart_element(chart_config config);
        ~chart_element() override;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // image, 이름, 요약이다. 요약의 최솟값·최댓값은 생성자에서 세어 `arrange` 전에도 답한다.
        [[nodiscard]] access_info accessibility() const override;
        // 배치 뒤 플롯 사각형이다 (물리 픽셀).
        [[nodiscard]] rect_f plot_bounds() const noexcept;

    private:
        // 자료 요약과 배치 결과다. 큰 자료의 열 표처럼 공개 헤더에 드러낼 까닭이 없는 값을 함께 든다.
        struct state;
        class plot_part;

        std::unique_ptr<state> state_;
        plot_part* plot_ { nullptr };
    };
}
