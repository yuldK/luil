#pragma once

#include "luil/text/text_format.h"

#include <cstddef>
#include <string_view>

namespace luil::text {
    // 구간과 링크를 불변식대로 다듬는다. begin 순으로 세우고, 빈 구간과 `none` 구간을 버리고, 맞닿은 같은 서식을 합친다.
    //  - 겹친 구간은 앞 구간이 그 몫을 갖는다 (밖에서 온 조각이 어긋나도 불변식이 선다).
    void normalize_text_format(text_format& format);

    // [from, to] 안에 선 구간 경계를 글자소 경계로 맞춘다. 글자소 한가운데의 경계는 그 글자소의 끝으로 민다.
    //  - 글자소는 첫 바이트의 서식을 따른다. 굵은 `e` 뒤에 친 결합 부호는 굵다.
    //  - 끝으로만 밀므로 구간 차례가 뒤집히지 않는다. 빈 구간은 버린다.
    //  - 편집이 닿은 자리만 묻는다. 경계 하나가 그 줄을 훑으므로 글 전체를 맞추면 구간 수 곱하기 줄 길이가 든다.
    void snap_text_format(text_format& format, std::u8string_view text, std::size_t from, std::size_t to);
}
