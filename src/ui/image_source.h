#pragma once

#include "luil/ui/image_decode.h"

#include "include/core/SkData.h"
#include "include/core/SkRefCnt.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace luil {
    // 인코딩된 그림을 코덱 앞까지 데려오는 자리다 (래스터 코덱·SVG·Lottie가 함께 쓴다).
    //
    // 경로를 읽는 규칙(NUL·폴더·크기 상한·걸음마다 묻는 취소)과 바이트를 빌리는 규칙,
    // 실패를 짓는 모양이 형식마다 따로 서면 셋은 반드시 어긋난다. 그래서 한 벌이다.
    //  - 이름 공간을 따로 둔다. 짧은 이름(`make_error`)이 `luil` 전체에 퍼지지 않게,
    //    부르는 파일이 using 선언으로 골라 쓴다.
    namespace image_input {
        // 실패 하나를 짓는다. 갈래와 글이 언제나 함께 서고 장 번호는 0이다.
        [[nodiscard]] image_decode_error make_error(image_decode_error_kind kind, std::u8string message);
        [[nodiscard]] image_decode_error cancelled_error();
        // 앱이 접었는가. 갈고리가 없으면 묻지 않는다 (`image_decode_options::cancelled`).
        [[nodiscard]] bool decode_cancelled(const image_decode_options& options);
        // 파일을 통째로 읽는다 (1 MiB 걸음마다 취소를 묻고, 256 MiB를 넘으면 거절한다).
        // 실패하면 null과 `error`다.
        [[nodiscard]] sk_sp<SkData> read_image_file(std::u8string_view path, const image_decode_options& options, image_decode_error& error);
        // 바이트를 복사하지 않고 빌린다. 코덱이 이 호출 안에서 끝나야 한다.
        [[nodiscard]] sk_sp<SkData> borrow_image_bytes(std::span<const std::uint8_t> bytes, const image_decode_options& options, image_decode_error& error);
    }
}
