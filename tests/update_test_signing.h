#pragma once

#include "update_fixtures.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Windows test만 쓰는 시험 서명이다. 시험 키 A(update_test_key.h)로 매니페스트를 그 자리에서 서명한다.
//  - 키는 고정 자료라 새로 짓지 않는다. CNG의 ECDSA는 서명마다 난수를 써 같은 payload도 서명 바이트가 다르다.
namespace luil::testing {
    // payload를 서명해 r||s 64바이트를 준다. 실패하면 비었다.
    [[nodiscard]] std::optional<std::array<std::uint8_t, 64>> sign_update_payload(std::span<const std::uint8_t> payload);
    // payload 글을 키 A로 서명한 봉투다. `der`이면 DER 칸, 아니면 raw 칸이다.
    [[nodiscard]] std::u8string make_signed_update_envelope(std::u8string_view payload, bool der = false);
}
