#pragma once

#include "luil/text/text_edit.h"
#include "luil/text/text_format.h"
#include "luil/ui/text_document.h"
#include "luil/ui/ui_events.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 서식 있는 글 칸의 명령이다 (docs/rich-edit-design.md).
    //  - 앞의 다섯(굵게·기울임·밑줄·코드·서식 지우기)은 `apply_rich_text_command`가 한다.
    //  - 링크는 앱이 주소를 받은 뒤 `set_rich_text_link`로 건다. 복사·잘라내기·붙여넣기는 교환 쪽 helper의 몫이다
    //    (docs/rich-edit-exchange-design.md).
    enum class rich_text_command
    {
        bold,
        italic,
        underline,
        code,
        clear_format,
        link,
        copy,
        cut,
        paste,
        paste_plain,
    };

    // 메뉴 체크와 도구 막대 단추의 켜짐이다 (`rich_text_state_of`).
    struct rich_text_state
    {
        bool bold { false };
        bool italic { false };
        bool underline { false };
        bool code { false };
        bool link { false };

        [[nodiscard]] bool operator==(const rich_text_state&) const = default;
    };

    // 지금 편집 상태의 서식 켜짐이다.
    //  - 고른 구간이 있으면 구간 전체에 그 서식이 켜졌는지다. 링크는 고른 구간이 한 링크 안에 들 때다.
    //  - 없으면 다음에 넣는 글이 받을 서식(`text::text_edit_typing_marks`)이다. 링크는 caret이 링크 안이거나 바로 뒤일 때다.
    [[nodiscard]] rich_text_state rich_text_state_of(const text::text_edit_state& state);

    // 서식 명령 하나를 편집 상태에 적용한다. 바뀐 것이 있으면 참이다.
    //  - 고른 구간이 모두 그 서식이면 끄고 아니면 켠다. 서식만 바꾸는 실행 취소 한 걸음이다 (`text_edit_set_format`).
    //  - 고른 구간이 없으면 다음 글자 서식(`typing_marks`)만 뒤집는다. 걸음이 아니다.
    //  - `clear_format`은 고른 구간의 굵기·기울임·밑줄·코드를 지운다. 링크는 그대로다 (링크 걷기는 `set_rich_text_link(state, {})`).
    //  - 서식 다섯만 한다. `link`·`copy`·`cut`·`paste`·`paste_plain`에는 거짓이다.
    bool apply_rich_text_command(text::text_edit_state& state, rich_text_command command);

    // 고른 구간에 그 주소의 링크를 건다. 빈 주소는 링크를 걷는다. 바뀐 것이 있으면 참이다.
    //  - 고른 구간이 없으면 caret이 선 링크(안이거나 바로 뒤)의 주소를 바꾸거나 걷는다. 그런 링크가 없으면 거짓이다.
    //  - 서식만 바꾸는 실행 취소 한 걸음이다.
    bool set_rich_text_link(text::text_edit_state& state, std::u8string_view destination);

    // 편집 상태의 서식을 여러 줄 칸의 `text_area_config::spans`·`links`로 옮긴다.
    //  - 앱은 칸 하나에 하나를 쥐고, 편집을 적용한 같은 logic turn에서 `update`를 부른 뒤 두 값을 칸 설정에 넘긴다.
    //  - 서식 포인터가 지난번과 같으면 지난 vector를 그대로 둔다. 칸의 wrap cache와 배치 cache가 같은 구간을 다시 견주지 않는다.
    //  - 지난 서식의 포인터를 쥐어 둔다. 풀린 서식의 주소에 새 서식이 앉아 같은 포인터로 보이는 일이 없다.
    //  - 서식이 없으면 둘 다 빈 포인터다.
    class rich_text_presenter
    {
    public:
        void update(const text::text_edit_state& state);
        [[nodiscard]] std::shared_ptr<const std::vector<text_span>> spans() const noexcept;
        [[nodiscard]] std::shared_ptr<const std::vector<text_link>> links() const noexcept;

    private:
        bool updated_ { false };
        std::shared_ptr<const text::text_format> format_ {};
        std::shared_ptr<const std::vector<text_span>> spans_ {};
        std::shared_ptr<const std::vector<text_link>> links_ {};
    };

    // 서식 있는 글 조각이다. `format`의 offset은 `text` 기준이다 (`rich_text_from_html`).
    struct rich_text_fragment
    {
        std::u8string text {};
        text::text_format format {};

        [[nodiscard]] bool operator==(const rich_text_fragment&) const = default;
    };

    // 남의 HTML을 읽을 때의 한도다.
    struct rich_text_html_limits
    {
        // 넘으면 HTML을 버리고 글로 물러선다 (빈 조각).
        std::size_t max_bytes { std::size_t { 16 } * 1024 * 1024 };
        // 더 깊은 요소는 서식 없이 글만 읽는다. 깊이를 세는 데 메모리를 쓰지 않는다.
        std::size_t max_depth { 256 };
    };

    // 글을 조각 HTML로 쓴다 (복사·저장, docs/rich-edit-exchange-design.md).
    //  - 문서 머리 없이 `white-space: pre-wrap`을 건 바깥 `<span>` 하나와 `<b>`·`<i>`·`<u>`·`<code>`·`<a href>`·`<br>`만 쓴다.
    //  - 서식이 같은 조각마다 태그를 열고 닫는다. 바깥부터 `<a>`, `<code>`, `<b>`, `<i>`, `<u>` 차례라 늘 바르게 닫힌다.
    //  - 글의 `&`·`<`·`>`, 주소의 `&`·`<`·`>`·`"`를 이스케이프한다. 줄 바꿈은 `<br>`이다. 빈 글은 빈 HTML이다.
    //  - `rich_text_from_html(rich_text_to_html(t, f))`는 `{ t, f }`다 (`f`가 `text_format`의 불변식을 지킬 때).
    [[nodiscard]] std::u8string rich_text_to_html(std::u8string_view text, const text::text_format& format);

    // HTML을 걸러 서식 있는 글로 읽는다 (붙여넣기·불러오기). 허용 밖의 요소는 버리거나 글만 남기고, 바깥 자원을 읽지 않는다.
    //  - 서식은 굵게·기울임·밑줄·코드와 `http:`·`https:`·`mailto:` 링크뿐이다. `style`의 굵기·기울임·밑줄·공백 규칙이 태그보다 앞선다.
    //  - 블록은 줄 바꿈, 표 칸 사이는 탭이다. 스크립트·스타일·문서 머리 같은 요소는 내용째, 그림·주석은 버린다.
    //  - `max_bytes`를 넘거나 깨진 UTF-8이면 빈 조각이다.
    [[nodiscard]] rich_text_fragment rich_text_from_html(std::u8string_view html, const rich_text_html_limits& limits = {});

    // 고른 구간의 글과 HTML을 한 클립에 넣는 요청이다 (`app_host::post_clipboard_request`). 고른 구간이 없으면 빈 요청이다.
    [[nodiscard]] clipboard_data_copy_request rich_text_copy_request(const text::text_edit_state& state);

    // 클립보드 답(`clipboard_data`)을 caret에 넣는다. 넣었으면 참이다. 실행 취소 한 덩어리다.
    //  - `plain`이거나 HTML이 없으면 글을 넣는다 (넣은 글의 서식 규칙을 따른다).
    //  - 아니면 거른 조각을 자기 서식째 넣는다 (`text_edit_insert_formatted`). 거른 조각이 비면(한도, 깨진 UTF-8) 글로 물러선다.
    //  - 그림은 넣지 않는다. 그림만 든 답에는 거짓이고 아무것도 바꾸지 않는다.
    bool rich_text_paste(text::text_edit_state& state, const clipboard_data& data, bool plain = false);
}
