#include "luil/ui/virtual_list_element.h"

#include "luil/ui/ui_tree.h"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// 가상 목록의 기하 함수가 모델 크기에 비례하는 비용을 잰다 (virtual-list-design.md의 "모델 크기 계측").
//
// 기본 실행에서는 숨는다(`[.]`). Release 구성에서 이렇게 돌린다.
//   luil_core_tests "[benchmark][list]" --benchmark-samples 20
// 잰 값은 문서의 표에 옮기고, 누적 높이 캐시를 넣을지는 그 표로 정한다.
namespace {
    [[nodiscard]] std::u8string row_key(std::size_t index)
    {
        std::u8string digits {};
        do
        {
            digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + index % 10u));
            index /= 10u;
        } while (index != 0u);
        return u8"row-" + digits;
    }

    // 행 높이가 섞인 모델이다. 열 줄마다 한 줄이 두 배 높이다 — 가변 높이의 최악은 아니어도
    // 기본 높이로 나눠 답할 수 없는 모양이다.
    [[nodiscard]] std::shared_ptr<const std::vector<luil::virtual_list_item>> make_model(const std::size_t count)
    {
        auto items { std::make_shared<std::vector<luil::virtual_list_item>>() };
        items->reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            luil::virtual_list_item item {};
            item.key = row_key(index);
            item.label = item.key;
            item.height = index % 10u == 0u ? 48.0f : 0.0f;
            items->push_back(std::move(item));
        }
        return items;
    }

    void measure_model(const std::size_t count)
    {
        const std::shared_ptr<const std::vector<luil::virtual_list_item>> model { make_model(count) };
        const std::span<const luil::virtual_list_item> items { *model };
        constexpr float row_height { 24.0f };
        const float total { luil::virtual_list_content_height(items, row_height) };
        // 끝자락이 최악이다. 앞에서부터 더해 가며 찾기 때문이다.
        const float end_offset { total - 600.0f };

        BENCHMARK("content height")
        {
            return luil::virtual_list_content_height(items, row_height);
        };
        BENCHMARK("visible range at the end")
        {
            return luil::virtual_list_visible_range(items, row_height, end_offset, 600.0f, 2);
        };
        BENCHMARK("row span of the last row")
        {
            return luil::virtual_list_row_span(items, row_height, items.size() - 1u);
        };
        // frame 하나가 실제로 하는 일이다. 공유 모델이라 복사는 없고, 커서는 끝에 서 있다.
        BENCHMARK("build and arrange one frame at the end")
        {
            luil::virtual_list_config config {};
            config.owner = u8"bench";
            config.shared_items = model;
            config.row_height = row_height;
            config.scroll_offset = end_offset;
            config.cursor = model->back().key;
            return luil::make_arranged_tree(std::make_unique<luil::virtual_list_element>(std::move(config)), { 0.0f, 0.0f, 400.0f, 600.0f }, 1.0f);
        };
    }
} // namespace

TEST_CASE("Virtual list geometry with one hundred thousand rows", "[.][benchmark][list]")
{
    measure_model(100'000u);
}

TEST_CASE("Virtual list geometry with one million rows", "[.][benchmark][list]")
{
    measure_model(1'000'000u);
}
