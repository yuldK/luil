#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luil::win32 {
    // OS 끌어 놓기의 원시 데이터 객체(`ui_action_context::native_drop_data`, Win32 `IDataObject*`)에서 내용을 꺼내는
    // 도우미다 (os-dragdrop-design.md).
    //  - 모두 **`on_drop` 안의 UI thread**에서 부른다. OLE는 내용을 `Drop` 안에서 꺼내라고 하고, 그 밖에서는 객체가 이미
    //    풀렸을 수 있다.
    //  - 형식 이름은 `drag_payload::formats`의 것이다 (등록 형식은 OS 이름, 표준 형식은 `CF_*` 상수 이름).
    //  - 실패는 빈 값이다. source가 그 형식을 싣지 않았거나, 매체를 읽지 못했거나, 상한을 넘었다.
    //  - 원시 객체는 그대로 `IDataObject*`로 바꿔 직접 써도 된다. 도우미는 흔한 셋(가상 파일·바이트·UTF-16 글)만 덮는다.

    // 가상 파일 하나다 (`FileGroupDescriptorW`). 브라우저가 끄는 그림, 메일 첨부, 압축 파일의 항목처럼 디스크에 없는 파일이다.
    struct dropped_virtual_file
    {
        // source가 준 이름이다. 폴더 경로(`a\b.txt`)가 섞일 수 있다 — 저장하는 앱이 걸러서 쓴다.
        std::u8string name {};
        // source가 크기를 알렸으면 그 값이다.
        std::optional<std::uint64_t> size {};
        bool directory { false };
    };

    // 한 번에 읽을 내용의 기본 상한이다 (64 MiB). 밖의 프로그램이 정하는 매체가 메모리를 정하지 못하게 묶는다.
    inline constexpr std::size_t default_drop_data_limit { 64u * 1024u * 1024u };

    // 가상 파일 목록이다 (`FileGroupDescriptorW`). 순서가 `FileContents`의 색인이다.
    [[nodiscard]] std::vector<dropped_virtual_file> drop_virtual_files(void* data);

    // 그 형식의 내용을 바이트로 읽는다. `HGLOBAL`과 `IStream` 매체를 읽는다.
    //  - `index`는 `FORMATETC::lindex`다. 가상 파일의 내용(`FileContents`)은 `drop_virtual_files`의 색인을 준다. 그 밖은 -1이다.
    //  - `limit`보다 길면 빈 값이다 (잘린 내용을 온전한 것처럼 돌려주지 않는다).
    //  - `HGLOBAL` 매체는 할당 크기만큼 읽는다. 할당은 내용보다 클 수 있어 꼬리가 붙는다 — 가상 파일은
    //    `dropped_virtual_file::size`가 있으면 그 길이로 자른다. 글 형식은 `drop_data_text`가 첫 NUL에서 끊는다.
    [[nodiscard]] std::optional<std::vector<std::byte>> drop_data_bytes(void* data, std::u8string_view format, int index = -1, std::size_t limit = default_drop_data_limit);

    // UTF-16 글 형식의 내용을 UTF-8로 읽는다 (`CF_UNICODETEXT`, `UniformResourceLocatorW` …). 첫 NUL에서 끝난다.
    [[nodiscard]] std::optional<std::u8string> drop_data_text(void* data, std::u8string_view format, std::size_t limit = default_drop_data_limit);
}
