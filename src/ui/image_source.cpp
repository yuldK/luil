#include "ui/image_source.h"

#include "include/core/SkData.h"
#include "include/core/SkRefCnt.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace luil {
    namespace image_input {
        namespace {
            // 인코딩 바이트의 상한이다 (256 MiB).
            // 픽셀 상한과 별개다 — 파일을 통째로 메모리에 올려 코덱에 물리기 때문이다.
            // 이것도 앱이 바꾸지 않는다 (픽셀 상한과 같은 성격이다 — image_decode.cpp).
            constexpr std::uint64_t maximum_encoded_bytes { 256u * 1024u * 1024u };

            // 파일을 읽는 한 걸음의 크기다 (1 MiB).
            //
            // 256 MiB를 한 번에 읽으면 그동안 아무것도 묻지 못한다 — 걸음을 잘라야
            // 취소가 **읽는 중에도** 듣는다. 걸음이 너무 잘면 물음이 많아지고 너무
            // 굵으면 접히는 데 오래 걸리는데, 1 MiB면 요즘 디스크에서 한 걸음이
            // 밀리초 단위라 어느 쪽도 아니다.
            constexpr std::size_t image_read_chunk_bytes { 1u * 1024u * 1024u };
        }

        // 실패 하나를 짓는 자리다 (`make_http_error`가 이미 선 그 모양이다 —
        // src/net/winhttp/winhttp_error.cpp:29-37). 갈래와 글이 **언제나 함께** 서므로
        // 갈래만 바꾸고 글을 두고 오는 자리가 생기지 않는다.
        [[nodiscard]] image_decode_error make_error(const image_decode_error_kind kind, std::u8string message)
        {
            return { kind, std::move(message), 0 };
        }

        [[nodiscard]] image_decode_error cancelled_error()
        {
            return make_error(image_decode_error_kind::cancelled, u8"The image decode was cancelled.");
        }

        // 앱이 접었는가. **갈고리가 없으면 묻지 않는다** (없음이 곧 스위치다 —
        // image_decode.h).
        //
        // 갈고리는 밖에서 오는 구현이라 값을 읽어 답하는 것 이상을 하지 않기로
        // 되어 있고, 던지지 않기로 되어 있다 — 그 계약을 여기서 다시 감싸지
        // 않는다 (`http_text_transcoder`를 부르는 자리와 같은 규약이다).
        [[nodiscard]] bool decode_cancelled(const image_decode_options& options)
        {
            return options.cancelled != nullptr && options.cancelled();
        }

        // 파일을 통째로 읽어 코덱에 물릴 바이트로 만든다.
        //
        // 경로 변환에 `utf8_to_utf16`이 필요 없는 자리다 — `std::filesystem::path`가
        // UTF-8을 네이티브 와이드로 옮긴다. 그 대가로 "UTF-8이 아니다"라는 갈래가
        // 사라지고 잘못된 경로는 "열지 못했다"로 떨어진다.
        [[nodiscard]] sk_sp<SkData> read_image_file(const std::u8string_view path, const image_decode_options& options, image_decode_error& error)
        {
            if (path.empty())
            {
                error = make_error(image_decode_error_kind::path_empty, u8"The image path is empty.");
                return nullptr;
            }
            // NUL이 든 경로는 거절한다. 네이티브 경로가 거기서 잘려 **다른 파일**이
            // 열리는데, 그것은 오류로 보이지 않고 엉뚱한 그림으로 보인다.
            if (path.find(u8'\0') != std::u8string_view::npos)
            {
                error = make_error(image_decode_error_kind::path_invalid, u8"The image path contains a null character.");
                return nullptr;
            }

            std::filesystem::path native {};
            try
            {
                // 짓기만 하는 자리라 지금 표준 라이브러리에서는 던지지 않지만,
                // **이 API가 던지지 않는다고 적어 두었다** — 계약을 구현 세부에
                // 기대지 않는다 (`std::bad_alloc`도 여기서 멈춘다).
                native = std::filesystem::path { std::u8string { path } };
            }
            catch (...)
            {
                error = make_error(image_decode_error_kind::file_unreadable, u8"Failed to open the image file.");
                return nullptr;
            }

            // 던지지 않는 오버로드들이다 — 없는 파일도 권한 없는 파일도 여기서 갈린다.
            //  - **폴더를 먼저 거른다.** `file_size`는 폴더에 0을 답하고 오류를
            //    적지 않아, 그냥 물으면 폴더 경로가 "파일이 비었다"로 보고된다 —
            //    사람이 읽고 고칠 수 있는 말이 아니다.
            std::error_code code {};
            const std::filesystem::file_status status { std::filesystem::status(native, code) };
            if (code || std::filesystem::is_regular_file(status) == false)
            {
                error = make_error(image_decode_error_kind::file_unreadable, u8"Failed to open the image file.");
                return nullptr;
            }
            const std::uintmax_t size { std::filesystem::file_size(native, code) };
            if (code)
            {
                error = make_error(image_decode_error_kind::file_unreadable, u8"Failed to open the image file.");
                return nullptr;
            }
            if (size == 0)
            {
                error = make_error(image_decode_error_kind::file_empty, u8"The image file is empty.");
                return nullptr;
            }
            if (size > maximum_encoded_bytes)
            {
                error = make_error(image_decode_error_kind::input_too_large, u8"The image file is larger than the 256 mebibyte decode limit.");
                return nullptr;
            }

            std::ifstream file { native, std::ios::binary };
            if (file.is_open() == false)
            {
                error = make_error(image_decode_error_kind::file_unreadable, u8"Failed to open the image file.");
                return nullptr;
            }
            // 상한이 앞에서 걸러 주므로 이 자리 잡기는 256 MiB를 넘지 않는다.
            // `MakeUninitialized`는 실패하면 null이 아니라 그 자리에서 죽는
            // Skia의 자리 잡기라 살펴볼 것이 없다 (`sk_malloc_throw`).
            sk_sp<SkData> data { SkData::MakeUninitialized(static_cast<std::size_t>(size)) };

            // **한 번에 읽지 않는다.** 느린 디스크(또는 네트워크 드라이브)의
            // 256 MiB는 그 자체로 초 단위이고, 한 번의 `read`가 도는 동안에는
            // 갈고리에게 물을 자리가 없다 — 그러면 취소가 "다 읽은 뒤에" 듣는다.
            // 걸음을 잘라 두면 창을 닫는 값이 파일 크기가 아니라 걸음 하나가 된다.
            auto* const writable { static_cast<char*>(data->writable_data()) };
            for (std::size_t offset { 0 }; offset < static_cast<std::size_t>(size);)
            {
                if (decode_cancelled(options))
                {
                    error = cancelled_error();
                    return nullptr;
                }
                const std::size_t remaining { static_cast<std::size_t>(size) - offset };
                const std::size_t chunk { remaining < image_read_chunk_bytes ? remaining : image_read_chunk_bytes };
                file.read(writable + offset, static_cast<std::streamsize>(chunk));
                // 크기를 물은 뒤 파일이 줄어들 수 있다 — 덜 읽은 바이트를 코덱에
                // 물리면 "잘린 그림"으로 보여, 실패를 그 자리에서 잡는다.
                if (file.gcount() != static_cast<std::streamsize>(chunk))
                {
                    error = make_error(image_decode_error_kind::file_unreadable, u8"Failed to open the image file.");
                    return nullptr;
                }
                offset += chunk;
            }
            return data;
        }

        [[nodiscard]] sk_sp<SkData> borrow_image_bytes(const std::span<const std::uint8_t> bytes, const image_decode_options& options, image_decode_error& error)
        {
            if (bytes.empty())
            {
                error = make_error(image_decode_error_kind::file_empty, u8"The image bytes are empty.");
                return nullptr;
            }
            if (bytes.size() > maximum_encoded_bytes)
            {
                error = make_error(image_decode_error_kind::input_too_large, u8"The image bytes are larger than the 256 mebibyte decode limit.");
                return nullptr;
            }
            // 바이트 쪽에는 읽는 고리가 없어 물을 자리가 여기 하나다. 그래도 묻는
            // 이유는 **줄 세워 둔 디코딩**이다 — 앞의 것이 도는 동안 접기로 한
            // 뒤의 것들이 코덱을 세우지도 않고 지나간다.
            //  - 검사가 먼저다. 접었다고 틀린 입력이 옳아지지 않는다.
            if (decode_cancelled(options))
            {
                error = cancelled_error();
                return nullptr;
            }
            // 복사하지 않는다 — 코덱은 이 호출이 끝나기 전에 죽으므로 빌린 버퍼가
            // 그보다 오래 살 필요가 없다.
            return SkData::MakeWithoutCopy(bytes.data(), bytes.size());
        }
    }
}
