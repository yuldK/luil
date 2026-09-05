#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace luil::win32 {
    // 실패 글 뒤에 HRESULT를 16진으로 붙인다 ("... (HRESULT=0x80070005)").
    //
    // 앞의 글은 사람이 읽을 문장이고 코드는 진단용이다 — 공개 API로 나가는 것은
    // 이 글 하나뿐이라(`std::u8string& error` 규약) HRESULT 타입 자체는 밖으로
    // 새지 않는다.
    //  - 렌더러와 이미지 디코딩이 둘 다 쓴다. 소비자가 둘이 되기 전에 한 자리로
    //    모았다 (`dropped_file_paths`와 같은 자리다).
    [[nodiscard]] std::u8string make_hresult_error(std::u8string_view message, HRESULT result);
} // namespace luil::win32
