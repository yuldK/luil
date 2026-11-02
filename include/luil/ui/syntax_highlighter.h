#pragma once

#include "luil/ui/text_document.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 줄 하나를 칠한다. 줄 바꿈은 `line`에 들지 않는다.
    //  - `state`는 윗줄이 끝난 뒤의 상태이고, 첫 줄은 `initial_state`다.
    //  - 구간은 줄 처음 기준 offset으로 `spans`에 더한다 (`spans`는 비워서 건넨다).
    //  - 돌려주는 값은 이 줄이 끝난 뒤의 상태다.
    // (줄 글, 입력 상태)의 순수 함수여야 한다. 줄 번호나 밖의 값을 읽지 않는다. 도우미가 같은 글과 같은 입력 상태의 줄은
    // 지난번과 같은 결과라고 보고 다시 부르지 않기 때문이다.
    using syntax_line_lexer = std::function<int(std::u8string_view line, int state, std::vector<text_span>& spans)>;

    // `update`에 예산을 주지 않는다는 값이다. 수렴할 때까지 칠한다.
    inline constexpr std::chrono::microseconds syntax_unlimited_budget { std::chrono::microseconds::max() };

    struct syntax_highlighter_config
    {
        // 비어 있으면 아무 구간도 칠하지 않는다.
        syntax_line_lexer lexer {};
        int initial_state { 0 };
        // 이보다 긴 줄은 렉서에 보내지 않고, 구간 없이 입력 상태를 그대로 다음 줄에 넘긴다.
        // 한 줄에 수 MB가 든 글(압축한 파일)에서 렉서 하나가 오래 서는 것을 막는다.
        std::size_t max_line_size { 20'000 };
    };

    struct syntax_highlight_stats
    {
        std::size_t lines { 0 };
        std::size_t spans { 0 };
        // 마지막 `update`·`advance`가 렉서에 보낸 줄 수다 (test·진단용). `max_line_size`를 넘어 건너뛴 줄은 세지 않는다.
        std::size_t relexed { 0 };
    };

    // 편집하는 코드 글의 구간 색을 편집에 맞춰 다시 칠한다 (docs/syntax-highlight-design.md).
    // 앱이 줄 하나를 칠하는 렉서를 주면, 바뀐 줄부터 렉서를 다시 돌려 `text_area_config::spans`에 넘길 구간을 만든다.
    //  - 줄은 LF, CRLF, 홀로 선 CR로 끝난다 (`text_area_element`의 배치가 줄을 나누는 규칙과 같다). 글이 줄 바꿈으로
    //    끝나면 마지막에 빈 줄이 하나 있다.
    //  - 렉서 출력은 도우미가 다듬는다. 줄 글 밖과 빈 구간은 버리고, begin 순으로 정렬하고, 겹친 뒤쪽을 자르고,
    //    양끝을 글자소 경계로 맞춘다. 같은 모양의 이웃 구간을 줄 바꿈 너머로 합치지 않는다.
    //  - thread-safe가 아니다. 앱 logic이 쥐는 상태이고, `spans()`가 돌려준 vector는 어느 thread에서나 읽는다.
    class syntax_highlighter
    {
    public:
        explicit syntax_highlighter(syntax_highlighter_config config);

        // 글이 바뀐 뒤 부른다 (`apply_text_edit`와 같은 logic turn에서, 칸을 짓기 전에).
        // 지난 호출의 글과 견주어 바뀐 줄부터 다시 칠한다.
        //  - 편집이 닿은 줄은 늘 칠한다. 그 뒤(꼬리)는 앞 줄이 끝난 상태가 지난번과 같아지는 줄에서 멈춘다.
        //  - 꼬리와 첫 불러오기는 `budget` 안에서만 칠하고 남기면 `pending()`이 참이다. 아직 칠하지 않은 줄은 지난 색이 남는다
        //    (offset은 늘 현재 글에 맞는다). 처음 열 때는 남은 색이 없어 맨 위부터 차례로 색이 든다.
        //  - 대기 중에 편집이 오면 그 편집의 상태가 앞의 대기분에 기대므로, 대기분 중 편집보다 앞선 줄도 함께 칠한다.
        //  - 예산을 주지 않으면 늘 수렴까지 칠한다.
        void update(std::u8string_view text, std::chrono::microseconds budget = syntax_unlimited_budget);
        // 남은 일을 `budget` 안에서 이어 받는다. 남은 것이 없으면 아무 일도 하지 않는다.
        // 예산이 아무리 작아도 대기 중인 첫 줄은 칠한다. 앱이 작은 예산으로 틱마다 불러도 끝내 다 칠해진다.
        void advance(std::chrono::microseconds budget);
        [[nodiscard]] bool pending() const noexcept;
        // 다른 문서를 열 때 앞 문서의 줄 기록을 버린다. 다음 `update`가 첫 불러오기가 된다.
        void reset();

        // 지금까지 칠한 구간이다 (begin 순서, 겹치지 않음, 문서 byte offset).
        // 앞 호출 뒤 바뀐 것이 없으면 같은 포인터를 돌려준다. 게시한 vector는 고치지 않는다 (`text_area_config::spans`의 계약).
        // 새 vector는 이 호출이 짓는다. 한 turn에 `update`가 여러 번이어도 한 번만 쓴다.
        [[nodiscard]] std::shared_ptr<const std::vector<text_span>> spans();
        [[nodiscard]] const syntax_highlight_stats& stats() const noexcept;

    private:
        // 줄 하나의 기록이다. 줄 바꿈까지 포함한 길이, 칠할 때 받은 입력 상태와 끝 상태, 평면 구간 vector에서 이 줄이 차지한 수다.
        // `lexed`가 거짓이면 아직 한 번도 칠하지 않은 줄이다.
        struct line_record
        {
            std::size_t length { 0 };
            int in_state { 0 };
            int end_state { 0 };
            std::uint32_t span_count { 0 };
            bool lexed { false };
        };

        void load(std::u8string_view text);
        // 글이 바뀐 구간을 줄 기록에 옮기고, 편집이 닿은 줄의 끝(이 줄 앞은 늘 칠한다)을 돌려준다.
        [[nodiscard]] std::size_t apply_change(std::size_t offset, std::size_t removed, std::size_t inserted, std::u8string_view text);
        void work(std::size_t forced_end, std::chrono::microseconds budget);

        syntax_highlighter_config config_;
        syntax_highlight_stats stats_ {};
        // 지난 글을 한 벌 쥐고 편집마다 바뀐 구간만 고쳐 쓴다.
        std::u8string text_ {};
        std::vector<line_record> lines_ {};
        // 줄 순서로 이어 붙인 구간이다. 문서 byte offset이다.
        std::vector<text_span> spans_ {};
        // 앞 줄과 이어지지 않을 수 있어 다시 봐야 하는 줄의 번호다 (오름차순). 첫 항목부터 이어서 칠한다.
        std::vector<std::size_t> pending_lines_ {};
        // 대기 중인 첫 줄의 처음 offset과 구간 자리다. 예산에 걸려 멈춘 자리를 적어 두어, 다음 `advance`가 맨 앞 줄부터
        // 다시 세지 않는다. 글이나 줄 기록을 건드리면 버린다.
        std::size_t resume_line_ { 0 };
        std::size_t resume_offset_ { 0 };
        std::size_t resume_span_ { 0 };
        bool resume_known_ { false };
        std::shared_ptr<const std::vector<text_span>> published_ {};
        // 게시한 뒤 구간을 만졌는지다. 만졌어도 값이 같으면 같은 포인터를 돌려준다.
        bool touched_ { false };
        bool loaded_ { false };
        std::vector<text_span> raw_ {};
    };
}
