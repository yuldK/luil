#pragma once

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace luil {
    // 플랫폼 IME가 동기로 읽고 쓰는 그림자 문서다.
    //
    // IME(TSF, Android의 InputConnection)는 글과 선택을 **그 자리에서** 읽고 쓰기를 요구하는데
    // 글 상태는 logic thread 소유이고 UI thread에는 snapshot으로만 온다. 그래서 초점을 가진
    // 칸 **하나**의 사본을 UI thread가 갖는다. 확정된 글의 진실은 언제나 logic에 있다.
    struct text_input_document
    {
        std::u8string text {};
        // UTF-8 byte offset이다. 플랫폼마다 단위가 다르다 (TSF는 UTF-16 ACP).
        std::size_t caret { 0 };
        std::size_t anchor { 0 };
        std::uint64_t applied_sequence { 0 };

        // 번호는 처리 완료 표식이고 문서의 내용은 아니다.
        [[nodiscard]] bool operator==(const text_input_document& other) const
        {
            return text == other.text && caret == other.caret && anchor == other.anchor;
        }
    };

    // 플랫폼 IME가 앱에 묻고 알리는 창구다 (TSF, Android). UI thread에서만 불린다.
    //  - 조합 중의 글은 `post_composition`으로 화면에만 보이고, 확정되면 `post_edit`의
    //    `replace_all` 하나로 초안이 바뀐다. 조합 단계가 undo 기록에 쌓이지 않는다
    //    (docs/concepts/text-input.md).
    class text_input_host
    {
    public:
        text_input_host() = default;
        text_input_host(const text_input_host&) = delete;
        text_input_host(text_input_host&&) = delete;
        text_input_host& operator=(const text_input_host&) = delete;
        text_input_host& operator=(text_input_host&&) = delete;
        virtual ~text_input_host() = default;

        // 지금 초점을 가진 텍스트 칸이다. 없으면 nullopt다.
        [[nodiscard]] virtual std::optional<text_input_target> focused_text_target() const = 0;
        // 그 칸의 확정된 글이다.
        [[nodiscard]] virtual text_input_document committed_document() const = 0;
        // 문서의 byte 구간이 그 칸이 사는 표면에서 차지하는 자리다 (물리 픽셀, 표면 좌표).
        // 후보 창이 조합 글자 밑에 붙는 근거다. offset은 넘긴 문서 기준이다 — 조합 중에는
        // 문서가 칸의 확정 글보다 길다. 알 수 없으면 nullopt다.
        [[nodiscard]] virtual std::optional<rect_f> text_rect(const text_input_document& document, std::size_t begin, std::size_t end) const = 0;
        // logic으로 보낸다.
        virtual void post_composition(text_composition_event event) = 0;
        virtual void post_edit(text_edit_request request) = 0;
    };
} // namespace luil
