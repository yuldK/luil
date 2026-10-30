#pragma once

#include "luil/theme/ui_style.h"

#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 한 줄 도우미(`draw_text`·`measure_text`)가 그리고 재기 전에 글의 줄바꿈을 바꾸는 규칙이다.
    // 둘이 **같은 함수를** 지나야 폭과 그림이 어긋나지 않는다. 규칙을 test가 글꼴 없이 확인하도록 노출한다.
    //  - 줄바꿈 하나는 CR, LF, 또는 CRLF 짝이다. CR·LF는 UTF-8의 이어지는 byte가 될 수 없으므로 잘못된
    //    UTF-8 위에서도 byte로 훑어 찾는다 — 잘못된 byte는 그대로 남아 그리기의 관문(`drawable_text`)이 고친다.

    // `space`·`symbol`의 바뀐 글이다. 줄바꿈이 없으면 `text`를 그대로 돌려주고 `storage`를 건드리지 않는다.
    //  - CRLF 짝을 하나로 접어 byte 길이가 원문과 달라진다. 한 줄 도우미는 byte offset을 밖으로 내지 않으므로
    //    (폭 하나만 돌려준다) 접어도 된다. offset을 지켜야 하는 입력·문서 보기는 이 함수를 쓰지 않는다.
    //  - `paragraphs`는 글을 바꾸지 않고 그대로 돌려준다. 줄을 나누는 것은 `single_line_paragraphs`다.
    [[nodiscard]] std::u8string_view single_line_text(std::u8string_view text, single_line_break mode, std::u8string& storage);

    // `paragraphs`가 그릴 줄들이다. 줄바꿈 byte는 빠지고 원문을 가리키는 조각만 남는다.
    // 줄바꿈이 없으면 원문 하나, 끝에 줄바꿈이 있으면 뒤에 빈 줄 하나가 선다 (문서 배치의 마지막 빈 줄과 같은 뜻이다).
    [[nodiscard]] std::vector<std::u8string_view> single_line_paragraphs(std::u8string_view text);
}
