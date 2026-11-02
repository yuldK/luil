#pragma once

// 이미지 페이지다: 왼쪽의 대시 테두리 영역에 그림 파일을 끌어다 놓거나 눌러서
// 파일 dialog로 고르면, 오른쪽 칸에 비율을 지켜 맞춘 미리 보기가 선다.
//
// 두 계기(드롭·파일 dialog)가 **같은 메시지 하나**를 낸다. 읽는 쪽은 그것이
// 어디서 왔는지 알 필요가 없다.
//
// 아래의 클립보드 줄은 연 그림을 이름 글과 함께 한 클립에 넣고(PNG는 logic thread에서 짓는다), 클립보드의 그림을
// 붙여 미리 보기 칸에 세운다 (image-clipboard-design.md). 상태는 label 글로 그려 UIA Name으로 되읽는다.

#include "demo/common.h"
#include "luil/ui/clipboard_image.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/image_element.h"
#include "luil/ui/label_element.h"
#include "luil/ui/ui_element.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace demo {
    // --- 이 페이지의 메시지 ---
    // 그림 하나를 읽자는 요청이다.
    // 드롭은 input thread의 액션이, 파일 dialog는 UI thread의 delegate가 낸다.
    struct image_open_intent
    {
        std::u8string path {};
    };

    // 연 그림을 클립보드에 넣는다. 셸이 받아 `make_copy_request`로 요청을 짓고 host에 넣는다 — 클립보드는 UI thread의
    // 몫이고 이 페이지는 host를 모른다.
    struct image_clipboard_copy_intent
    {
    };

    // 클립보드의 그림을 붙인다. 셸이 받아 `clipboard_data_paste_request`를 넣는다.
    struct image_clipboard_paste_intent
    {
    };

    // 클립보드에서 읽은 답이다 (`app_delegate::make_clipboard_data_message`). 페이지가 logic thread에서 푼다.
    struct image_clipboard_data_intent
    {
        luil::clipboard_data data {};
    };

    // 이 데모가 받는 그림 확장자다 (소문자, 점 없이).
    //
    // 화면의 안내 줄·드롭 수락·파일 dialog의 필터가 **이 목록 하나**를 쓴다.
    // 두 벌이면 언젠가 한쪽만 늘어나고, 안내와 실제가 갈리면 예제가 거짓말을 한다.
    //  - OS 코덱이 아니라 이 프로젝트가 함께 빌드한 Skia 코덱의 공통분모다.
    //    tiff는 열리지 않고, ico는 png 코덱 선택에 따라 달라지므로 넣지 않는다.
    inline constexpr std::array<std::u8string_view, 8> image_extensions { u8"apng", u8"png", u8"jpg", u8"jpeg", u8"bmp", u8"wbmp", u8"gif", u8"webp" };

    // 경로의 확장자가 그 목록에 있는가 (대소문자를 가리지 않는다).
    [[nodiscard]] bool is_supported_image(std::u8string_view path);

    class images_page
    {
    public:
        // 이 페이지의 메시지를 처리했으면 참이다.
        bool handle(const luil::app_message& message);

        // 페이지 내용이다. width·height는 내용 영역의 논리 픽셀 크기다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 연 그림과 그 이름 글을 한 클립에 넣는 요청이다. PNG를 여기서(logic thread) 짓는다. 그림이 없으면 nullopt다.
        //  - 무엇을 보냈는지는 상태 줄(`images-clipboard-copy`)에 남긴다. 넣기는 답이 없다.
        [[nodiscard]] std::optional<luil::clipboard_data_copy_request> make_copy_request();

        // 클립보드에 그림이 있는가를 적는다 (`clipboard_change::has_image`). 셸이 파일 페이지와 함께 이 페이지에도 넘긴다.
        void set_clipboard_has_image(bool has_image) noexcept;

    private:
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_clipboard_row() const;
        // 클립보드의 답을 풀어 미리 보기 칸에 세운다.
        void paste(const luil::clipboard_data& data);

        [[nodiscard]] std::unique_ptr<luil::ui_element> make_drop_column(float width) const;
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_preview() const;
        [[nodiscard]] std::unique_ptr<luil::label_element> make_status() const;

        // 그림은 만들 때 한 번 준비되는 불변 값이라 페이지가 들고 있는다 —
        // frame마다 config에 실리는 것은 손잡이뿐이다 (image-design.md).
        // 파일은 밖에서 오므로 실패할 수 있다: 이유를 함께 들어 화면에 남긴다.
        //  - **그림과 이유가 함께 설 수 있다.** 이 페이지는 잘린 파일을 받기로
        //    했으므로(`image_incomplete_policy::accept`) 미리 보기가 서 있는데도
        //    이유가 남아 있는 자리가 있다 — 상태 줄이 그 둘을 갈라 읽는다.
        luil::ui_animated_image animation_ {};
        luil::image_playback playback_ {};
        std::u8string picture_name_ {};
        luil::image_decode_error picture_error_ {};
        // 장 목록과 가운데 장 하나다 (image-frames-scaling-design.md). 목록은 픽셀 없이 읽고, 가운데 장은 그 장
        // 하나만 합성까지 마쳐 읽는다 — 미리 보기 줄이 "어느 장을 보고 있는가"를 함께 보인다.
        luil::image_frame_table frames_ {};
        luil::ui_image middle_frame_ {};
        // 미리 보기 첫 장을 200 KB 안의 WebP로 담아 본 결과다 (image-encode-design.md의 바이트 한도).
        std::u8string webp_summary_ {};
        // 클립보드 줄의 상태다. 그림 있음은 첫 변화 알림 전에는 모른다 (Win32는 시작할 때 알리지 않는다).
        std::optional<bool> clipboard_has_image_ {};
        std::u8string clipboard_copy_ { u8"그림 복사: 아직 안 함" };
        std::u8string clipboard_result_ { u8"붙여넣기 결과: 아직 없음" };
    };
}
