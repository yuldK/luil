#pragma once

#include "luil/text/text_edit.h"
#include "luil/text/text_format.h"
#include "luil/ui/text_document.h"

#include <memory>
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
}
