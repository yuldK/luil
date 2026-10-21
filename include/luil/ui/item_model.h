#pragma once

#include "luil/ui/virtual_list_element.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace luil {
    // 앱 모델을 목록·표의 항목으로 옮기는 순수 함수들이다 (item-model-design.md).
    //
    // Qt의 `QAbstractItemModel`·`QSortFilterProxyModel`이 하는 일 중 **불변 상태와 메시지 구조에 맞는 것만** 둔다.
    // element가 아니라 앱 상태를 계산하는 함수다 — 모델은 앱이 들고, 앱은 frame마다 이 함수로 목록에 실을 항목을
    // 다시 계산한다. 변경 신호(rowsInserted 같은 것)는 없다. tree가 frame마다 새로 지어지므로 "무엇이 바뀌었는가"는
    // 항목 키가 말한다.

    // 노드의 자식을 불러온 상태다 (지연 로딩).
    enum class node_load
    {
        // 자식이 다 있다. 자식이 없으면 잎이다.
        loaded,
        // 아직 묻지 않았다. 자식이 없어도 가지다 (펼치면 불러온다).
        unloaded,
        // 묻는 중이다. 펼쳐 두면 "불러오는 중" 줄이 선다.
        loading,
        // 묻다가 실패했다. 펼쳐 두면 실패 줄이 선다 (다시 시도의 자리).
        failed,
    };

    // 계층 모델의 노드 하나다 (앱의 모델에서 frame마다 짓거나, 앱이 이 모양 그대로 든다).
    struct tree_node
    {
        // 항목 키다. 모델 전체에서 유일해야 한다 (`virtual_list_item::key` 규약).
        std::u8string key {};
        std::u8string label {};
        std::vector<tree_node> children {};
        node_load load { node_load::loaded };
        bool enabled { true };
    };

    // 평탄화한 줄의 종류다.
    enum class tree_row_kind
    {
        // 모델의 노드다.
        node,
        // 펼친 노드의 자식을 불러오는 중이다. 고를 수 없다 (비활성 항목).
        loading,
        // 펼친 노드의 자식을 불러오지 못했다. 고를 수 있다 — 키보드가 그 줄에 서서 다시 시도를 실행할 수 있어야 한다.
        failed,
    };

    // 평탄화한 줄 하나다.
    struct tree_row
    {
        // 목록에 그대로 싣는 항목이다 (깊이·펼침 포함).
        virtual_list_item item {};
        tree_row_kind kind { tree_row_kind::node };
        // 상태 줄(불러오는 중·실패)이 딸린 노드의 키다. 노드 줄이면 자기 키다.
        std::u8string node {};
    };

    // 상태 줄의 키에 붙는 꼬리다. 상태 줄 키는 `노드 키 + U+001F + 꼬리`다 — 표의 부품 키와 같은 구분자라 앱 키와 겹치지
    // 않는다 (앱 키에 U+001F를 쓰지 않는 한).
    inline constexpr std::u8string_view tree_loading_suffix { u8"\u001Floading" };
    inline constexpr std::u8string_view tree_failed_suffix { u8"\u001Ffailed" };

    struct tree_flatten_options
    {
        // 상태 줄의 이름이다. 앱의 언어로 준다 (라이브러리는 글을 정하지 않는다).
        std::u8string loading_label {};
        std::u8string failed_label {};
        // 형제 순서다. 비어 있으면 모델 순서다. 같은 부모 아래 형제끼리만 견준다 (계층은 그대로다).
        std::function<bool(const tree_node& left, const tree_node& right)> less {};
        // 남길 노드다. 비어 있으면 모두 남긴다.
        //  - 남긴 노드의 조상은 함께 남는다 — 맞는 파일이 접힌 폴더 안에 있어도 그 길이 보인다 (Qt의 재귀 필터).
        //  - 남긴 노드의 자손은 그 자손도 맞아야 남는다.
        std::function<bool(const tree_node& node)> keep {};
        // 필터로 남은 자손이 있는 가지를 펼친 것으로 본다 (찾는 동안 맞는 것이 다 보이게). 펼침 상태(`expanded`)는
        // 바꾸지 않는다 — 필터를 지우면 앱이 들고 있던 펼침으로 돌아간다.
        bool expand_matches { false };
    };

    // 계층 모델을 목록의 줄로 편다 (가상 목록·표의 트리 열).
    //  - 가지는 자식이 있거나 아직 다 불러오지 않은(`loaded`가 아닌) 노드다. 펼침은 `expanded`에 키가 있는가다.
    //  - 펼친 가지의 자식이 그 아래에 깊이 + 1로 선다. 접힌 가지의 자식은 줄이 되지 않는다.
    //  - 펼친 가지가 `unloaded`·`loading`이면 그 아래에 불러오는 중 줄, `failed`면 실패 줄이 선다.
    //  - 자식이 없고 다 불러온 펼친 가지는 빈 가지로 남는다 (펼침 상태 그대로).
    [[nodiscard]] std::vector<tree_row> flatten_tree(std::span<const tree_node> roots, const std::unordered_set<std::u8string>& expanded, const tree_flatten_options& options = {});

    // 줄들의 항목만이다 (`virtual_list_config::items`에 싣는다).
    [[nodiscard]] std::vector<virtual_list_item> tree_row_items(std::span<const tree_row> rows);

    // 펼쳐 두었는데 아직 묻지 않은(`unloaded`) 노드들의 키다 (모델 순서). 앱이 이 키들에 불러오기를 시작하고 노드를
    // `loading`으로 바꾼다. 접힌 가지 아래는 보지 않는다.
    [[nodiscard]] std::vector<std::u8string> tree_nodes_to_load(std::span<const tree_node> roots, const std::unordered_set<std::u8string>& expanded);

    // 정렬·필터를 거친 보기의 순서다 (원본 색인의 차례).
    //  - 원본은 그대로 두고 보기만 다시 줄 세운다. 정렬 열을 바꾸거나 필터를 지워도 원본을 다시 읽지 않는다.
    struct view_order
    {
        // 보기의 n번째 줄이 원본의 몇 번째인가다.
        std::vector<std::size_t> source_index {};

        // 원본 색인이 보기의 몇 번째인가. 걸러져 없으면 nullopt다. 보기 길이에 비례한다.
        [[nodiscard]] std::optional<std::size_t> view_index(std::size_t source) const noexcept;

        [[nodiscard]] bool operator==(const view_order&) const = default;
    };

    // `keep`이 남긴 항목을 `less`로 줄 세운다. 같은 항목끼리는 원본 순서다 (안정 정렬) — 같은 값이 많은 열로 정렬해도
    // 새 frame마다 줄이 뒤섞이지 않는다.
    //  - `less`·`keep`이 비어 있으면 원본 순서·전부다.
    //  - 항목 형은 첫 인자에서만 정한다 (`std::type_identity_t`). 람다를 그대로 넘겨도 형 추론이 깨지지 않는다.
    template<typename item_type>
    [[nodiscard]] view_order sort_filter(const std::span<const item_type> items,
        const std::function<bool(const std::type_identity_t<item_type>& left, const std::type_identity_t<item_type>& right)>& less,
        const std::function<bool(const std::type_identity_t<item_type>& item)>& keep = {})
    {
        view_order order {};
        order.source_index.reserve(items.size());
        for (std::size_t index = 0; index < items.size(); ++index)
            if (keep == nullptr || keep(items[index]))
                order.source_index.push_back(index);
        if (less != nullptr)
            std::stable_sort(order.source_index.begin(), order.source_index.end(), [&](const std::size_t left, const std::size_t right) { return less(items[left], items[right]); });
        return order;
    }

    template<typename item_type>
    [[nodiscard]] view_order sort_filter(const std::vector<item_type>& items,
        const std::function<bool(const std::type_identity_t<item_type>& left, const std::type_identity_t<item_type>& right)>& less,
        const std::function<bool(const std::type_identity_t<item_type>& item)>& keep = {})
    {
        return sort_filter(std::span<const item_type> { items }, less, keep);
    }

    // 비동기 요청 하나의 표다. 결과에 함께 실려 와 **늦게 온 결과**를 가른다.
    struct request_token
    {
        std::uint64_t value { 0 };

        [[nodiscard]] bool operator==(const request_token&) const = default;
    };

    // 키마다 마지막으로 낸 요청을 기억하는 장부다 (앱 상태에 둔다).
    //
    // 폴더를 펼쳐 목록을 묻고, 결과가 오기 전에 접었다 다시 펼치면 요청이 둘이다. 먼저 낸 것이 늦게 오면 새 결과를
    // 옛 결과가 덮는다. 요청마다 표를 받고, 결과가 오면 그 키의 **마지막** 표와 같은지 보고 다르면 버린다.
    //  - 같은 키의 새 요청은 앞 요청을 무효로 만든다. 다른 키의 요청은 서로 모른다.
    //  - 빈 키 하나만 쓰면 검색어 후보·미리 보기처럼 "마지막 것만 산다"는 요청 하나의 장부다.
    class request_ledger
    {
    public:
        // 이 키의 새 요청 표다. 그 키의 앞 표는 이제 낡았다.
        [[nodiscard]] request_token issue(std::u8string_view key);
        // 이 표가 그 키의 마지막 요청인가. 끝냈거나 취소한 키, 낸 적 없는 표는 거짓이다.
        [[nodiscard]] bool current(std::u8string_view key, request_token token) const;
        // 결과를 받아들였다. 맞는 표면 그 키를 지우고 참이다 — 같은 결과가 두 번 와도 한 번만 받는다.
        [[nodiscard]] bool settle(std::u8string_view key, request_token token);
        // 그 키의 요청을 거둔다 (접었다·떠났다). 뒤에 오는 결과는 모두 낡았다.
        void cancel(std::u8string_view key);
        // 기다리는 요청 수다.
        [[nodiscard]] std::size_t pending() const noexcept;

    private:
        std::unordered_map<std::u8string, std::uint64_t> latest_ {};
        std::uint64_t next_ { 0 };
    };
} // namespace luil
