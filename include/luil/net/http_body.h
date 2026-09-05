#pragma once

#include "luil/net/http_media_type.h"
#include "luil/ui/image_decode.h"
#include "luil/ui/image_element.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luil::net {
    // UTF-8이 아닌 charset을 UTF-8로 옮기는 갈고리다.
    //
    // 표를 이 파이프라인 안에 박아 두지 않는 이유는 표가 크고 틀리기 쉽기
    // 때문이다. 대신 갈고리 하나를 두고 기본 구현을 `codepage_text_transcoder`가
    // 댄다 — 그래서 euc-kr은 그냥 되고, 순수 판정만 잠그는 test는 가짜 갈고리를
    // 꽂아 서버도 OS 표도 없이 규칙을 확인한다.
    //  - 실패하면 빈 글을 돌려주고 `error`에 이유를 적는다. 성공하면 `error`는
    //    건드리지 않는다 (`decode_image_bytes`와 같은 규약). **던지지 않는다.**
    //  - 옮기기는 했으나 잃은 글자가 있으면 **글과 함께** `error`에 그 사실을
    //    적는다 — 그 글은 `http_body::text`에 실리고 그 사실은 `parse_error`에
    //    남는다 ("비어 있지 않다고 값이 없는 것은 아니다"라는 그쪽 계약과 같다).
    //  - 돌려주는 글은 반드시 올바른 UTF-8이어야 한다. 공개 `std::u8string`의
    //    계약이고, 깨진 것을 담으면 그리기가 죽는다 (text/utf8_text.h).
    using http_text_transcoder = std::function<std::u8string(std::span<const std::uint8_t> bytes, std::u8string_view charset, std::u8string& error)>;

    // OS의 코드 페이지 표로 charset을 옮기는 기본 변환기다.
    //
    // 우리가 표를 지지 않기로 한 자리의 반쪽이다. euc-kr(949)·shift_jis(932)·
    // windows-1252(1252)·utf-16이 그냥 되고, 모르는 이름은 실패로 답한다.
    //  - `http_client`가 `http_client_config::transcode`가 비어 있으면 이것을 꽂는다.
    //    앱은 아무것도 하지 않아도 되고, 다른 규칙을 원하는 앱만 자기 것을 넣는다.
    [[nodiscard]] http_text_transcoder codepage_text_transcoder();

    // 이 요청의 그림을 어떻게 풀 것인가.
    struct http_image_options
    {
        // 디코드 시점의 축소 상한이다. 뜻과 규칙은 `image_decode_options` 그대로다.
        //  - **네트워크에서 오는 그림에는 거의 언제나 채워야 한다.** 밖에서 오는
        //    4000×3000 사진 하나가 48 MB이고, 움직이는 것이면 장 수가 곱해진다.
        image_decode_options decode {};
        // 참이면 `decode_animated_image_bytes`로 푼다 (기본값).
        // 거짓이면 첫 장만 `decode_image_bytes`로 풀어 한 장짜리 값으로 담는다 —
        // 목록의 섬네일처럼 움직일 이유가 없는 자리에서 장 수만큼의 메모리와
        // 시간을 아낀다. 어느 쪽이든 값이 서는 자리는 `http_body::image` 하나다.
        bool animated { true };
    };

    // 바이트를 값으로 바꾸는 규칙 묶음이다. 요청마다 다르게 줄 수 있다.
    struct http_body_parse_options
    {
        // 채우면 Content-Type도 냄새도 보지 않고 이 갈래로 푼다.
        // **서버가 틀린 것을 앱이 아는 자리다** — json을 `text/plain`으로 내는
        // API가 흔하고, 그것을 라이브러리가 몰래 고쳐 주는 것과 앱이 시켜서 하는
        // 것은 성격이 아주 다르다 (`media_type_invites_sniffing`의 주석과 짝이다).
        std::optional<http_body_kind> assume_kind {};
        http_image_options image {};
        // json으로 풀기를 시도할 바이트 상한이다. 넘으면 갈래는 `json`인 채로
        // `parse_error`가 서고 `json`은 비어 있다 — 바이트는 그대로 남는다.
        //  - 몸 전체 상한(`http_request::max_body_bytes`)과 따로다. 64 MiB를 받는
        //    것은 되어도 64 MiB를 DOM으로 펴는 것은 아니다.
        std::size_t max_json_bytes { 16u * 1024u * 1024u };
        // json 중첩 깊이 상한이다. 넘는 가지는 짓지 않고 `parse_error`가 선다.
        //  - **파서가 아니라 파괴자 때문에 필요하다.** 파싱은 제 스택을 쓰지만
        //    깊게 중첩된 값을 **버릴 때**는 재귀라, `[[[[...`를 십만 겹 보낸
        //    서버가 우리 stack을 넘긴다. 짓지 않으면 버릴 것도 없다.
        int max_json_depth { 256 };
        // 참이면 html도 `text`를 함께 채운다 (규칙은 text와 같다).
        // 기본은 거짓이다 — html의 소비자는 webview이고 그쪽은 바이트와 charset을
        // 받는다. 미리 UTF-8로 옮겨 두면 파서가 필요로 하는 charset을 버리는
        // 셈이고, 2 MB짜리 문서가 메모리에서 두 벌이 된다. 눈으로 보려는 앱만 켠다.
        bool html_as_text { false };
    };

    // 답의 몸 한 덩이다. **갈래 하나와 슬롯 넷이 한 struct에 산다.**
    //
    // variant도 lazy accessor도 아닌 이유가 이 타입의 요점이다.
    //  - variant면 원본 바이트가 갈래에 밀려난다. 그런데 실패를 사람에게 보일 때
    //    필요한 것이 정확히 그 바이트다 ("JSON이라더니 이렇게 왔다"). 게다가
    //    `nlohmann::json`과 `ui_animated_image`가 든 variant는 어차피 큰 쪽만큼
    //    크므로 아끼는 것도 없다.
    //  - **lazy accessor는 더 나쁘다.** 늦게 풀면 푸는 자리가 *부르는 쪽의 thread*가
    //    되는데, 이 값을 받아 보는 쪽은 대개 그리기다 — 그림 디코딩이 수십 ms에서
    //    초 단위인 것(image_decode.h)을 생각하면 창이 그만큼 멎는다. 값은
    //    **채널을 건너기 전에 이미 다 풀려 있어야** 하고, 그것을 강제하는 것이
    //    이 struct다.
    // 무거운 슬롯은 전부 참조 공유라 복사가 싸고, 만들어진 뒤 바뀌지 않는다 —
    // thread를 건너도 새 동기화가 없다 (`ui_image`와 같은 계약이다).
    struct http_body
    {
        http_body_kind kind { http_body_kind::empty };
        http_body_kind_source kind_source { http_body_kind_source::absent };
        // **언제나 있다.** 어떤 갈래로 풀렸든, 풀리다 실패했든 받은 바이트 그대로다
        // (압축은 이미 풀려 있다). 몸이 없으면 비어 있다.
        //  - null일 수 있으므로 읽는 것은 `data()`다.
        std::shared_ptr<const std::vector<std::uint8_t>> bytes {};
        // `kind == json`이고 파싱이 성공했을 때만 선다.
        //  - **shared_ptr인 것이 판정이다.** 공개 헤더가 `json_fwd.hpp`만 들이므로
        //    umbrella 헤더를 쓰는 소비자가 nlohmann 본체를 열지 않아도 이 타입을
        //    지나칠 수 있고, 값이 참조 공유라 복사가 싸며, `null` 문서와 "풀리지
        //    않았다"가 섞이지 않는다 — json의 `null`은 어엿한 값이라 `is_null()`
        //    로는 그 둘을 못 가른다.
        std::shared_ptr<const nlohmann::json> json {};
        // `kind == image`이고 디코딩이 성공했을 때만 선다.
        // 움직이지 않는 파일도 여기로 온다 (한 장짜리 값이고 `animated()`가 거짓).
        ui_animated_image image {};
        // `kind == text`(또는 `html_as_text`를 켠 html)이고 글로 옮겨졌을 때만 선다.
        // **언제나 올바른 UTF-8이다.** 읽는 것은 `as_text()`다.
        std::shared_ptr<const std::u8string> text {};
        // 값이 바이트를 그대로 옮긴 것이 아니면 이유가 적힌다 (진단용 영문).
        //
        // **비어 있지 않다고 값이 없는 것은 아니다.** 깨진 UTF-8을 U+FFFD로 바꿔
        // 담은 글이 그 자리다 — 글은 쓸 수 있고, 원문과 다르다는 사실만 여기 남는다.
        // 슬롯이 비었는지는 각 슬롯이 답한다 (`json == nullptr`·`image.valid()`).
        std::u8string parse_error {};

        // 없으면 빈 span·빈 view다 (부르는 쪽이 null을 검사하지 않게 한다).
        [[nodiscard]] std::span<const std::uint8_t> data() const noexcept;
        [[nodiscard]] std::u8string_view as_text() const noexcept;

        // 우리가 푸는 갈래이고 그 값이 실제로 서 있는가.
        [[nodiscard]] bool parsed() const noexcept;
        // 움직임이 필요 없는 자리를 위한 한 장이다 (`image.frame_image(0)`).
        // 그림이 없으면 빈 이미지다 — 범위 검사를 부르는 쪽에 떠넘기지 않는다.
        [[nodiscard]] const ui_image& still_image() const noexcept;
    };

    // 바이트를 몸으로 바꾼다. **파싱이 사는 유일한 자리다.**
    //
    // 순수 함수라 서버도 thread도 없이 test가 전부를 잠근다 (`decoded_image_size`·
    // `animation_frame_at`이 이미 선 그 자리다). 던지지 않는다 — nlohmann의 예외는
    // 이 함수의 구현 파일 안에서 값으로 바뀐다.
    //
    // 갈래를 정하는 차례는 넷이다.
    //  1. 바이트가 비었으면 `empty`다 (Content-Type이 무엇이라 적혔든).
    //  2. `assume_kind`가 있으면 그것이다 (`assumed`).
    //  3. `media_type_invites_sniffing`이면 냄새를 맡는다 (`sniffed`).
    //  4. 아니면 `classify_media_type`이다 (`declared`).
    //
    // 갈래별로 하는 일:
    //  - json — charset은 보지 않는다 (형식이 UTF-8로 못 박혀 있다). BOM이 있으면
    //    넘긴다. 실패해도 갈래는 `json`이고 이유가 `parse_error`에 남는다.
    //  - image — `decode_animated_image_bytes`(또는 `animated == false`면
    //    `decode_image_bytes`)를 그대로 부른다. 상한과 실패 규칙이 파일에서 읽을
    //    때와 **같다** — 네트워크에서 왔다고 다른 규칙을 두지 않는다.
    //  - text — charset이 없거나 `utf-8`·`us-ascii`면 그대로 읽는다. 올바르지
    //    않은 UTF-8이면 `utf8_replace_invalid`로 담고 그 사실을 `parse_error`에
    //    적는다 (빈 글을 돌려주면 화면에서 "빈 응답"과 구별되지 않는다).
    //    그 밖의 charset은 `transcode`에게 넘기고, 갈고리가 없으면 옮기지 않고
    //    이유만 적는다 — 바이트는 온전히 남으므로 앱이 자기 규칙으로 읽는다.
    //  - html — 바이트와 charset만 남긴다. `html_as_text`를 켰으면 text 규칙을
    //    함께 건다.
    //  - bytes·empty — 아무것도 풀지 않는다.
    //
    //  - **걸리는 시간이 몸 크기에 비례한다.** 큰 JSON은 수십 ms이고 장이 많은
    //    gif는 초 단위까지 간다. UI thread에서 부를 자리가 아니다
    //    (`decode_animated_image_bytes`와 같은 규칙이고 같은 이유다).
    //    `http_client`는 자기 thread에서 이것을 부르므로, 그 길로 온 몸은 앱이
    //    어느 thread에서 받아도 이미 다 풀려 있다.
    [[nodiscard]] http_body decode_http_body(std::vector<std::uint8_t> bytes, const http_media_type& content_type, const http_body_parse_options& options);
    [[nodiscard]] http_body decode_http_body(std::vector<std::uint8_t> bytes, const http_media_type& content_type, const http_body_parse_options& options, const http_text_transcoder& transcode);
} // namespace luil::net
