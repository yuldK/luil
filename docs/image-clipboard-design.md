# 그림 클립보드

앱이 그림을 클립보드에 넣고, 다른 앱이 넣은 그림을 읽는다. 글과 그림을 한 클립에 함께 넣을 수 있다. [파일 목록 클립보드](file-clipboard-design.md)와 같은 요청/응답 길을 탄다. 요청은 UI thread가 실행하고, 읽은 답은 `app_delegate` 훅이 앱 메시지로 짓는다. 공개 형식은 [ui_events.h](../include/luil/ui/ui_events.h)의 `clipboard_data_copy_request`·`clipboard_data_paste_request`·`clipboard_data`·`clipboard_image_format`과 `clipboard_change::has_image`, 새 헤더 `include/luil/ui/ui_image.h`(옮겨 온 `ui_image`), 새 헤더 `include/luil/ui/clipboard_image.h`의 `decode_clipboard_image`와 `max_clipboard_image_bytes`, `app_delegate::make_clipboard_data_message`, `ui_platform_capabilities::clipboard_image_copy`다.

이 노드는 한 클립에 여러 형식을 싣는 요청 모양과 Win32의 여러 형식 블록 쓰기 helper를 처음 세운다. R3 rich_edit가 HTML·RTF를 그 위에 얹는다.

## 문제

- 클립보드는 글과 파일 목록뿐이다. `clipboard_request`는 그 넷이다([ui_events.h](../include/luil/ui/ui_events.h) 660줄). Win32는 `CF_UNICODETEXT`와 `CF_HDROP`만 다룬다([win32_clipboard.h](../src/win32/win32_clipboard.h)). Android는 글만 다룬다([android_clipboard.h](../src/android/android_clipboard.h)).
- 앱이 직접 짤 자리가 없다. 클립보드는 UI thread 자원인데 주 창 프로시저는 공개 헤더에 없다. 앱이 `OpenClipboard`를 따로 부르면 `scoped_clipboard`의 재시도와 반쪽 형식 거두기(win32_clipboard.cpp 297~350줄)를 다시 짠다.
- 그림을 그대로 실을 수 없다. `ui_image`는 [image_element.h](../include/luil/ui/image_element.h) 44줄에 있고 그 헤더는 `ui_element.h`를 include한다. `ui_element.h`는 12줄에서 `ui_events.h`를 include하므로 `ui_events.h`의 구조체는 `ui_image`를 값으로 들 수 없다.
- 창 계층은 인코더·디코더를 부르지 않는다. 창이 있는 모든 앱에 링크되기 때문이다([window_surface.cpp](../src/win32/window_surface.cpp) 77~81줄, [이미지 인코딩](image-encode-design.md)). 픽셀 읽기는 된다. R1 작업 표시줄의 [icon_from_image.cpp](../src/win32/icon_from_image.cpp) 3줄이 core의 `ui/image_pixels.h`를 include해 `read_image_pixels`를 부른다.
- Android 글 붙여넣기는 그림 항목을 글로 붙인다. `read_text_from_clipboard`가 `ClipData.Item.coerceToText`를 부르고(android_clipboard.cpp 84줄), 그림 URI는 `content://…` 글이 되어 칸에 들어간다.

## 공개 형식

`ui_image`와 `make_rgba_image`를 `include/luil/ui/ui_image.h`로 옮긴다. 그 클래스는 `image_backing` 전방 선언과 `shared_ptr`만 쓴다(image_element.h 44~65줄). `image_element.h`가 새 헤더를 include해 기존 호출은 바뀌지 않는다. `ui_events.h`가 새 헤더를 include한다. 새 공개 헤더 둘(`ui_image.h`, `clipboard_image.h`)은 `include/luil/luil.h`의 알파벳 순서 자리(`choice_group_element.h` 뒤, `ui_events.h` 뒤)에 더한다. [verify_umbrella_header.cmake](../cmake/verify_umbrella_header.cmake)가 `luil_umbrella_header` 검사로 빠뜨림을 막는다.

```cpp
// include/luil/ui/ui_events.h, `clipboard_change` 다음
enum class clipboard_image_format
{
    none,    // 그림이 없다
    encoded, // 그림 파일 바이트다 (Win32 "PNG", Android의 image/* 내용). `decode_image_bytes`가 푼다
    dib,     // Windows DIB다 (`CF_DIBV5`·`CF_DIB`, 파일 머리 없음)
};

// 여러 형식을 한 클립에 넣는다. 빈 칸의 형식은 넣지 않고, 모두 비면 아무 일도 없다.
struct clipboard_data_copy_request
{
    std::u8string text {};
    // 그림이다. Win32는 `CF_DIBV5`로 넣는다. 비면 그림 형식이 없다.
    ui_image image {};
    // 앱이 미리 지은 PNG(`encode_png`)다. `image`가 있을 때만 Win32 "PNG" 형식으로 그대로 넣는다.
    std::vector<std::uint8_t> png {};

    // 그림은 같은 값(같은 backing)인가로 견준다. 정의는 헤더의 inline이라 견주는 앱이 디코더를 링크하지 않는다.
    [[nodiscard]] bool operator==(const clipboard_data_copy_request& other) const noexcept;
};

struct clipboard_data_paste_request
{
    std::uint32_t tag { 0 }; // 답에 그대로 실린다
    [[nodiscard]] bool operator==(const clipboard_data_paste_request&) const noexcept = default;
};

// `clipboard_data_paste_request`의 답이다. 그림은 풀지 않은 바이트로 온다.
struct clipboard_data
{
    std::u8string text {}; // 줄바꿈과 탭을 지킨다. 없으면 비었다
    clipboard_image_format image_format { clipboard_image_format::none };
    // 앱 메시지로 옮겨도 복사되지 않게 공유한다. `image_too_large`면 비었다.
    std::shared_ptr<const std::vector<std::uint8_t>> image {};
    bool image_too_large { false };
    std::uint32_t tag { 0 };
    std::uint32_t sequence { 0 }; // Win32 `GetClipboardSequenceNumber`. Android는 0이다
    [[nodiscard]] bool operator==(const clipboard_data&) const noexcept = default;
};

// `clipboard_change` 끝: 지금 클립보드에 그림이 있는가 (Win32 "PNG"·`CF_DIB`).
bool has_image { false };

using clipboard_request = std::variant<clipboard_copy_request, clipboard_paste_request, clipboard_files_copy_request, clipboard_files_paste_request,
    clipboard_data_copy_request, clipboard_data_paste_request>;
```

```cpp
// include/luil/ui/clipboard_image.h (core)
// 클립보드에서 그림 하나로 읽는 바이트의 상한이다. 디코더의 인코딩 바이트 상한과 같다 (결정 10).
inline constexpr std::size_t max_clipboard_image_bytes { 256u * 1024u * 1024u };

// 클립보드의 그림 바이트를 `ui_image`로 푼다. 상한·오류·thread 규칙은 `decode_image_bytes`와 같다. logic thread나 worker에서 부른다.
// 그림이 없으면(`none`이나 빈 바이트) `file_empty`, `image_too_large`면 `input_too_large`다.
[[nodiscard]] ui_image decode_clipboard_image(const clipboard_data& data, const image_decode_options& options, image_decode_error& error);
```

- `app_delegate`에 `[[nodiscard]] virtual app_message make_clipboard_data_message(const clipboard_data& data)`를 더한다. 기본은 빈 메시지다. 그림이 없어도 답은 온다.
- `ui_platform_capabilities` 끝에 `bool clipboard_image_copy { false }`를 더한다. 그림을 넣을 수 있는 플랫폼만 참이다(Win32).
- `input_action`은 고치지 않는다(결정 6). 요청은 logic thread가 `app_host::post_clipboard_request`로 넣는다.

비공개 파일은 이렇다.

| 파일 | 몫 |
| --- | --- |
| `src/ui/clipboard_image.cpp` (core) | `decode_clipboard_image`. DIB 머리 읽기와 RGBA 옮기기는 순수 함수다. Windows 헤더 없이 바이트 자리로 읽는다(`luil_core_portability`) |
| `src/ui/image_source.h`·`image_decode.cpp` (core) | `image_input::check_source_pixels`. 코덱과 같은 픽셀 상한과 같은 오류 글을 DIB 옮기기도 쓴다 (결정 12) |
| `src/win32/win32_clipboard.h`·`.cpp` | 여러 형식 블록을 한 번 열어 모두 넣거나 모두 거두는 `put_clipboard_blocks`(`SetClipboardData` 자리를 test가 바꿔 끼운다), `unicode_text_block`. 그림 클립보드가 같은 열기를 쓰도록 `open_clipboard`·`scoped_clipboard`·`scoped_global_lock`을 헤더로 낸다 |
| `src/win32/win32_image_clipboard.h`·`.cpp` | `ui_image`→`CF_DIBV5` 블록(`dibv5_block`), 요청→블록 목록(`clipboard_data_blocks`), 넣기(`copy_data_to_clipboard`), 그림·글 읽기(`read_clipboard_data`), `has_image` 판정(`clipboard_has_image`) |
| `src/android/android_clipboard.h`·`.cpp` | `read_clipboard_data`, 글 읽기의 그림 항목 거르기 |

## 규칙

**넣기(Win32).** `clipboard_data_copy_request`의 칸마다 블록을 짓고 `put_clipboard_blocks`가 한 번 열어 `EmptyClipboard` 뒤 차례로 넣는다. 하나라도 `SetClipboardData`가 실패하면 넣은 것도 거둔다. 파일 목록의 "둘째 형식이 실패하면 첫째도 거둔다"를 블록 수만큼 넓힌 것이다. 넣는 순서는 "PNG", `CF_DIBV5`, `CF_UNICODETEXT`다. `EnumClipboardFormats`가 이 순서로 내므로 받는 앱이 그림을 먼저 본다. `png`는 앞 8바이트가 PNG 서명일 때만 넣고 아니면 버린다. `CF_DIB`·`CF_BITMAP`은 OS가 `CF_DIBV5`에서 지어 준다. 블록은 즉시 짓는다(결정 4).

**DIBV5 블록.** `read_image_pixels`로 픽셀을 얻는다. SVG 그림은 자기 크기로 한 번 편다. 머리는 `BITMAPV5HEADER`(124바이트), 32비트, `BI_BITFIELDS`, 마스크 R `0x00FF0000`·G `0x0000FF00`·B `0x000000FF`·A `0xFF000000`, `LCS_sRGB`다. 높이는 양수(아래에서 위로)다. 위에서 아래로 쌓은 DIB를 잘못 읽는 앱이 있어서다. 색은 알파를 곱하지 않은 값이다(결정 5).

**읽기(Win32).** 한 번 열어 글과 그림과 번호를 함께 읽는다. 번호는 연 채 읽어 내용과 짝이 맞는다. 글은 `CF_UNICODETEXT`만 보고 `text_from_utf16_block`의 상한(`max_clipboard_text_chars`)을 따른다. 텍스트 칸 붙여넣기와 달리 파일 경로로 대신하지 않는다. 그림은 "PNG", `CF_DIBV5`, `CF_DIB` 순으로 처음 있는 것 하나다. "PNG"는 `encoded`, 나머지는 `dib`다. 블록은 `GlobalSize` 안에서만 읽고 256 MiB를 넘으면 복사하지 않고 `image_too_large`다. 디코더의 바이트 상한과 같은 값이다(image_decode.h 185줄).

**풀기.** `decode_clipboard_image`는 `encoded`를 `decode_image_bytes`에 넘긴다. `dib`는 머리(`BITMAPINFOHEADER`·V4·V5, 12바이트 `BITMAPCOREHEADER`는 `unsupported_format`)를 읽어 24·32비트 `BI_RGB`와 `BI_BITFIELDS`를 직접 RGBA로 옮겨 `make_rgba_image`를 부른다. 코덱이 없다. 팔레트·RLE·16비트 같은 나머지는 14바이트 BMP 파일 머리를 앞에 붙여 `decode_image_bytes`(Skia BMP)에 넘긴다. 머리가 말하는 크기가 블록보다 크면 거절한다. 픽셀 상한(`max_source_pixels`)은 메모리를 잡기 전에 본다. `max_width`·`max_height`는 다 편 뒤 `downscale_image`로 맞춘다.

**알파(읽기).** 알파 마스크가 있으면 곱하지 않은 값으로 읽는다. 32비트 `BI_RGB`는 넷째 바이트를 알파로 보되, 모든 픽셀이 0이면 불투명으로 본다. 그 바이트를 비워 두는 앱이 흔해 그대로 읽으면 그림이 사라진다. 알파 마스크가 있는 `BI_BITFIELDS`도 모든 알파가 0이면 같은 규칙을 따른다(결정 11). 곱한 값을 넣는 앱을 가려내는 규칙은 두지 않는다(실측).

**thread와 비용.** 넣기와 읽기는 UI thread에서 돈다. 넣기는 픽셀 복사와 BGRA 옮기기라 큰 그림(수천만 화소)은 수십 ms다. 읽기는 블록 복사뿐이고 풀기는 앱이 logic thread에서 한다. PNG 인코딩도 앱이 logic thread에서 한다(결정 2).

**변화 알림.** Win32 `post_clipboard_change`(win32_window.cpp 1866~1870줄)가 `has_image`를 채운다. 등록 형식 "PNG"나 `CF_DIB`가 있으면 참이다. `CF_BITMAP`만 넣은 앱도 OS가 `CF_DIB`를 지어 주므로 잡힌다. Android는 지금처럼 알림이 없다. `OnPrimaryClipChangedListener`는 인터페이스라 Java 구현이 든다.

**Android 읽기.** `getPrimaryClip`의 항목을 차례로 본다. 첫 `getText()`가 글이다. `getUri()`가 있고 `ContentResolver.getType`이 `image/`로 시작하는 첫 항목을 `openInputStream`으로 연다. 64 KiB `byte[]` 하나로 `read`를 되풀이해 `GetByteArrayRegion`으로 옮기고 256 MiB에서 멈춘다. 형식은 `encoded`다. 다른 앱이 넣은 URI의 읽기 권한은 `getPrimaryClip`이 내준다고 알려져 있다(실측 전). 읽기는 `android_main` thread에서 하고, 다른 프로세스의 provider가 느리면 그만큼 멈춘다(실측).

**Android 넣기.** 글 칸만 `newPlainText`로 넣고 그림은 버린다. `clipboard_image_copy`가 거짓이라 앱이 "그림 복사"를 감출 수 있다(결정 7).

**접근성·배율·테마·고대비·움직임 줄이기.** 그리는 요소가 없어 해당하지 않는다. 라이브러리가 넣는 기본 문구도 없다. 기본 문구 정책(G6)의 대상이 아니다.

## 결정

**1. 공개 모양.** 질문: 그림 요청을 어떤 꼴로 내는가.
- 길 a: 그림 전용(`clipboard_image_copy_request`·`paste`). 되는 것: 뜻이 좁고 분명하다. 대가: 글과 그림을 한 클립에 넣지 못한다. 하나씩 넣으면 뒤의 것이 앞의 것을 지운다. rich_edit가 HTML+글용 요청을 또 만든다.
- 길 b: 한 클립 여러 형식(`clipboard_data_copy_request`, 이 문서). 되는 것: 글+그림이 한 번에 들어가고, rich_edit는 `html`·`rtf` 칸을 구조체 끝에 더한다. 대가: 칸이 늘면 플랫폼마다 "이 칸을 넣는가"가 갈려 능력 표가 는다.
- 길 c: 형식 항목 목록(`std::vector<clipboard_item>`, 항목은 variant). 대가: 앱 코드가 길고 같은 형식 둘처럼 뜻 없는 값을 걸러야 한다.
- 권장: b. 대가: 칸마다 능력 값이 하나씩 늘 수 있다. **승인: 길 b (2026-11-02).**

**2. 복사가 나르는 그림 값.** 질문: 요청에 무엇을 싣는가.
- 길 a: `ui_image` + 앱이 고르는 PNG 바이트(이 문서). 되는 것: 창 계층은 픽셀만 읽어 `CF_DIBV5`를 짓고 인코더를 링크하지 않는다. PNG를 원하는 앱만 `encode_png`를 logic thread에서 부른다. 대가: `ui_image`를 `ui_image.h`로 옮긴다. 공개 헤더가 하나 는다.
- 길 b: PNG 바이트만. 대가: 창 계층이 `CF_DIBV5`를 지으려면 PNG를 풀어야 해 디코더가 모든 창 앱에 링크된다. "PNG"만 넣으면 그림판처럼 DIB만 읽는 앱에 붙지 않는다.
- 길 c: RGBA 바이트와 너비·높이. 대가: `ui_image`에서 픽셀을 꺼내는 공개 함수가 없어 앱이 그림을 다시 풀어야 한다.
- 이 아래 갈래로 `ui_image`를 전방 선언하고 `std::shared_ptr<const ui_image>`를 싣는 길도 있다. 헤더는 그대로지만 앱이 그림을 한 번 더 싼다.
- 권장: a와 헤더 옮기기. 대가: 공개 헤더 하나와 `image_element.h` 정리. **승인: 길 a (2026-11-02).**

**3. 붙여넣기 풀기 자리.** 질문: 읽은 그림을 누가 푸는가.
- 길 a: 플랫폼은 바이트와 꼴만 넘기고 앱이 `decode_clipboard_image`로 푼다(이 문서). 되는 것: 창 계층에 디코더가 없고 UI thread가 큰 그림에 멈추지 않는다. DIB는 코덱 없이 옮긴다. 대가: 앱 코드가 한 단계 길다.
- 길 b: UI thread가 풀어 `ui_image`를 넘긴다. 대가: 디코더가 모든 창 앱에 링크되고 큰 PNG는 수십 ms 멈춘다.
- 권장: a. 대가: 앱이 풀기를 한 번 부른다. **권장대로 진행.**

**4. Win32 넣는 형식.** 질문: 어떤 형식을 언제 짓는가.
- 길 a: "PNG"(앱이 줬을 때) + `CF_DIBV5`를 즉시 짓는다. 되는 것: 알파를 담고 OS가 `CF_DIB`·`CF_BITMAP`을 지어 준다. 대가: 큰 그림은 메모리를 두 벌 쥔다.
- 길 b: `CF_DIB` 24비트만. 대가: 알파를 잃는다.
- 길 c: 지연 렌더링(`WM_RENDERFORMAT`). 대가: 임자 창이 살아 있어야 하고 종료 때 `WM_RENDERALLFORMATS`를 다뤄야 한다. 그림을 그때까지 쥐어야 해 아끼는 것이 적다.
- 권장: a. OS가 지은 `CF_DIB`가 알파 없는 앱에서 검은 바탕으로 보이면 흰 바탕 24비트 `CF_DIB`를 따로 넣는다. 대가: 메모리 두 벌. **권장대로 진행.**

**5. 알파 규칙.** 질문: DIB의 알파를 어떻게 적고 읽는가.
- 길 a: 곱하지 않은 값으로 적고, 읽기는 모두 0인 알파만 불투명으로 고친다(이 문서). 되는 것: 아이콘 변환과 같은 규칙이다(icon_from_image.cpp 94줄). 대가: 곱한 값을 넣는 앱의 반투명 가장자리는 조금 밝게 읽힌다.
- 길 b: 곱한 값으로 적는다. 대가: 곱하지 않은 값을 기대하는 앱에서 가장자리가 어두워진다.
- 길 c: 알파를 버리고 불투명으로만 다룬다. 대가: 투명 PNG를 붙이면 바탕이 생긴다. "PNG" 형식이 있는 앱 사이에서는 영향이 없다.
- 권장: a. 대가: 앱마다 다른 것은 사람 확인으로 본다. **권장대로 진행.**

**6. 요청이 다니는 길.** 질문: element 액션이 그림 요청을 바로 돌려줄 수 있게 하는가.
- 길 a: `clipboard_request`에만 더하고 logic thread가 `post_clipboard_request`로 넣는다(이 문서). 되는 것: 컨트롤러(ui_interaction.cpp 4353~4360줄의 get_if 사슬)를 고치지 않는다. 그림은 앱 상태라 logic이 쥐고 있다. 대가: 단추 → 앱 메시지 → logic → 요청으로 한 단계 돈다.
- 길 b: `input_action` 끝에도 더하고 줄 3이 pump 분기 둘을 낸다. 대가: 줄 3과 병합 순서가 묶이고, 분기가 늦게 들어오면 요청이 소리 없이 버려진다.
- 권장: a. 입력 thread에서 만드는 여러 형식 복사(rich_edit의 Ctrl+C)가 생기면 그때 `input_action` 끝에 더하고 줄 3이 분기를 낸다. 대가: 한 단계 도는 길. **권장대로 진행.**

**7. Android 넣기.** 질문: Java 0줄로 그림을 넣을 수 있는가.
- 길 a: 읽기만 하고 넣기는 글만(이 문서). 대가: Android에서 그림 복사가 없다.
- 길 b: androidx `FileProvider.getUriForFile`을 JNI로 부르고 캐시 파일을 `ClipData.newUri`로 싣는다. 되는 것: luil Java는 0줄이다. 대가: 소비 앱 매니페스트에 provider와 `res/xml` 경로 파일이 들고, 설정 실수가 곧 파일 노출이다. 변화 알림이 없어 캐시 파일을 지울 때를 모른다.
- 길 c: `MediaStore`에 넣는다. 대가: 갤러리에 흔적이 남고 minSdk 26~28은 저장소 권한이 든다.
- 직접 provider를 두는 길은 `ContentProvider` 하위 클래스라 Java가 들어 막힌다.
- 권장: a. 수요가 있으면 b를 따로 단계로 낸다. 대가: Android 그림 복사 없음. **승인: 길 a (2026-11-02).**

**8. Android 글 붙여넣기의 그림 항목.** 질문: 텍스트 칸에 그림 URI가 `content://…`로 붙는 것을 두는가.
- 길 a: 둔다. 대가: 칸에 쓸모없는 주소가 들어간다.
- 길 b: `getText()`가 없고 URI 형식이 `text/`가 아니면 글이 없다고 본다. 되는 것: Win32는 그림만 든 클립보드에서 이미 아무것도 붙이지 않는다. 대가: 링크 주소만 든 URI 항목도 붙지 않는다.
- 권장: b. 대가: URI 붙여넣기가 필요한 앱은 `clipboard_data_paste_request`를 쓴다. **권장대로 진행.**

**9. 읽기 상한과 변화 알림.** 질문: 큰 그림과 "그림 있음" 표시를 어떻게 다루는가.
- 길 a: 고정 256 MiB 바이트 상한과 `has_image`(이 문서). 되는 것: 디코더 상한과 같아 넘긴 바이트는 디코더도 받는다. "그림 붙여넣기" 단추를 켜고 끌 근거가 생긴다. 대가: 상한이 손잡이가 아니다.
- 길 b: 요청에 상한을 싣는다. 대가: 올려도 디코더가 256 MiB에서 거절해 얻는 것이 없다(image_decode.h 94줄).
- 권장: a. 대가: 손잡이 없음. **권장대로 진행.**

**10. 읽기 상한의 자리.** 질문: Win32와 Android가 함께 쓰는 256 MiB를 어디에 두는가. 설계 단계에 없던 질문이라 구현하며 정했다.
- 길 a: 공개 헤더 `clipboard_image.h`의 `max_clipboard_image_bytes`. 되는 것: 두 플랫폼이 같은 값을 쓰고, 앱이 `image_too_large`를 사람에게 설명할 때 그 수를 읽는다. 대가: 공개 이름 하나.
- 길 b: 플랫폼 파일마다 같은 상수를 둔다. 대가: 두 벌이 어긋날 수 있다.
- 고른 것: a. 손잡이가 아니라 읽는 값이다(결정 9).

**11. 모두 0인 알파의 범위.** 질문: "모든 알파가 0이면 불투명" 규칙을 32비트 `BI_RGB`에만 거는가. 설계 단계에 없던 질문이라 구현하며 정했다.
- 길 a: 알파 자리가 있는 32비트 꼴 모두(`BI_RGB`와 알파 마스크가 있는 `BI_BITFIELDS`)에 건다. 되는 것: V5 머리에 알파 마스크만 적고 바이트는 비워 둔 앱의 그림도 사라지지 않는다. 대가: 정말로 다 투명한 그림은 불투명하게 읽힌다. 클립보드에 다 투명한 그림을 넣을 까닭은 드물다.
- 길 b: `BI_RGB`에만 건다. 대가: 위의 앱에서 붙인 그림이 보이지 않는다.
- 고른 것: a.

**12. DIB 옮기기의 픽셀 상한.** 질문: 코덱을 거치지 않는 DIB 옮기기가 `max_source_pixels`를 어떻게 지키는가. 설계 단계에 없던 질문이라 구현하며 정했다.
- 길 a: 기본 64 M와 오류 글을 쥔 image_decode.cpp가 `image_input::check_source_pixels`를 내고, 코덱 길(`check_native_size`)과 DIB 길이 함께 부른다. 되는 것: 상한과 글이 한 벌이다. 대가: 비공개 함수 하나.
- 길 b: clipboard_image.cpp가 기본값과 글을 다시 적는다. 대가: 두 벌이 어긋날 수 있다.
- 고른 것: a. 상한은 메모리를 잡기 전, 머리가 말하는 크기가 블록보다 큰지 보기 전에 본다. 머리만 거대한 블록은 "모자람"이 아니라 "너무 큼"으로 답한다.

**13. 모바일 데모의 요청 길.** 질문: 모바일 데모의 "클립보드 그림 붙여넣기" 단추가 요청을 어떻게 내는가. 설계 단계에 없던 질문이라 구현하며 정했다.
- 길 a: 단추가 앱 UI 명령을 내고, delegate(`execute_app_ui_command`)가 host에 `clipboard_data_paste_request`를 넣는다. 되는 것: 모바일 데모의 logic(`mobile_driver`)은 host를 들지 않는다. 기기 페이지의 권한 묻기·알림 보내기와 같은 길이다. 대가: 그림을 넣는 요청처럼 앱 상태를 싣는 요청에는 이 길이 맞지 않는다. 붙여넣기는 앱 상태를 싣지 않아 이 길로 충분하다.
- 길 b: delegate가 시작할 때 host를 logic에 넘긴다. 대가: 붙여넣기 하나를 위해 셸 구조가 바뀐다.
- 고른 것: a. 데스크톱 데모는 결정 6대로 logic thread가 `post_clipboard_request`로 넣는다.

## 플랫폼

### Win32

- [win32_window.cpp](../src/win32/win32_window.cpp)에서 고치는 자리: `execute_clipboard_request`(1813줄)의 두 분기, `post_clipboard_change`(1866줄)의 `has_image`, `publish_platform`(2156줄)의 `clipboard_image_copy = true`. 액션 분기(1300~1318줄)는 결정 6으로 고치지 않는다. 읽은 답은 파일 클립보드처럼 훅을 `try`로 감싸 부르고 `post_app_message`로 넣는다.
- 레이어드 표시와 Direct3D 합성 창은 같다. 창을 그리지 않는다.
- `CF_ENHMETAFILE`(벡터 메타파일)과 "image/png" 같은 다른 등록 이름은 읽지 않는다.

### Android

- 모두 기존 framework 클래스(`ClipboardManager`, `ClipData`, `ContentResolver`, `InputStream`)를 JNI로 부르므로 Java 소스는 0줄이다([Android 이식 계획](android-port-plan.md)의 Java 소스 방침). 지역 참조는 [java_vm.h](../src/android/java_vm.h)의 `jni_local_frame`, 예외는 `jni_failed`다.
- [android_app.cpp](../src/android/android_app.cpp)는 `execute_clipboard`(1617줄)의 두 분기만 고친다. 넣기 요청은 글 칸만 쓰고, 읽기는 `read_clipboard_data`의 답을 훅으로 넘긴다. `dispatch`(1604~1611줄)는 고치지 않는다.
- Android 10부터 초점을 가진 앱만 읽고, 12부터 읽을 때 시스템 알림이 뜬다. 글 읽기와 같다.
- 그림 넣기와 변화 알림은 unsupported다(결정 7, 규칙).

## 다른 노드와의 약속

- **인자 넘기기·os_notify(줄 4, 앞).** `win32_window.cpp`, `app_delegate.h`, `ui_platform.h`를 두 노드 뒤에 직렬로 고친다. 병합 순서는 인자 넘기기, os_notify, image_clip이다. 훅은 os_notify의 `make_notification_message` 뒤, 능력은 그 노드의 `notifications` 뒤에 더한다. os_notify 문서 216줄의 "`ui_image`→Bitmap 변환을 공용 헤더로 꺼낸다"는 이 노드가 Android에 그림을 넣지 않아 필요 없다. 꺼내지 않는다.
- **rich_edit(줄 2, R3).** `clipboard_data_copy_request`와 `clipboard_data` 끝에 `html`·`rtf` 칸을 더한다. 기존 칸의 순서와 뜻은 바꾸지 않는다. Win32는 `put_clipboard_blocks`에 블록을 더하고, 읽기는 이 노드의 한 번 열기 안에서 읽는다. Android는 `ClipData.newHtmlText`로 글 칸과 함께 넣는다. 입력 thread에서 만드는 복사가 필요하면 결정 6의 b를 그때 줄 3과 함께 낸다.
- **ink(줄 3, 같은 라운드).** 잉크 그림 복사는 `render_ink_image`가 낸 `ui_image`를 이 요청에 싣는다. `ui_events.h`에서 이 노드는 include 한 줄, `clipboard_change` 끝, 새 구조체, `clipboard_request` 끝만 고친다. `pen_sample`과 `pointer_*_event`, `input_action`, `raw_input_event`의 구성원 순서는 건드리지 않는다. `android_app.cpp`에서 ink는 `copy_motion`과 축 켜기, 이 노드는 `execute_clipboard`다.
- **inspector(줄 3).** `android_app.cpp`의 `render()` 한 줄과 겹치지 않는다. 그 노드가 필요로 하는 `win32_window.cpp`의 frame 칸 한 줄은 줄 4가 통합 단계에서 별도 커밋으로 내고([인자 넘기기](single-instance-args-design.md)가 맡는다) 이 노드 커밋에 섞지 않는다.
- **`image_element.h`.** 이번 라운드에 이 헤더를 고치는 노드는 이 노드뿐이다. ink는 반환 형식으로 `ui_image`를 쓸 뿐이라 옮긴 뒤에도 그대로 빌드된다.
- **파일 클립보드.** 구현 커밋이 [file-clipboard-design.md](file-clipboard-design.md)의 어휘 표에 `has_image`를, 지원하지 않는 플랫폼 절에 이 문서로 가는 한 줄을 더한다. [이미지 인코딩](image-encode-design.md)에 클립보드가 PNG를 앱에서 받는 까닭 한 줄을 더한다.
- **데모 공용 파일.** `examples/demo_main.cpp`와 `examples/mobile_demo/app.cpp`의 훅 연결은 여러 줄이 고친다. `examples/mobile_demo/basics_page.*`는 줄 1의 msg_box(대화 상자 460~573행 치환)와 rating(249행 근처 행)도 고친다. 라이브러리 커밋과 데모 연결 커밋을 나누고 데모 연결은 통합 때 직렬로 쌓는다. `basics_page.*`와 `tests/CMakeLists.txt`의 `basics_page.cpp` 등록은 msg_box, rating, 이 노드 순이다. 등록 줄은 먼저 쌓이는 msg_box 연결이 넣는다. `docs/README.md` 색인도 통합 때 붙인다.

## 확인

- `tests/clipboard_image_tests.cpp`(`luil_core_tests`, Windows와 Android 기기에서 돈다): 24·32비트 `BI_RGB`와 V5 `BI_BITFIELDS`의 아래에서 위로·위에서 아래로 블록이 같은 RGBA가 되는지, 행 4바이트 정렬, 모든 알파 0이 불투명이 되는지, 머리가 말하는 크기보다 짧은 블록과 음수·0 크기의 거절, 픽셀 상한이 메모리 전에 걸리는지, 8비트 팔레트 블록이 BMP 길로 풀리는지, `encoded`의 PNG가 `decode_image_bytes`와 같은 픽셀인지(`sample_image_bytes.h`), `none`이 빈 그림과 오류인지.
- `tests/win32_image_clipboard_tests.cpp`(`luil_tests`): `CF_DIBV5` 블록의 머리 크기·마스크·`LCS_sRGB`·양수 높이·곱하지 않은 픽셀, SVG 그림이 제 크기로 펴지는지, PNG 서명이 아니면 "PNG"를 넣지 않는지. `[system]` 태그 test는 파일 클립보드 test처럼 메시지 전용 창을 임자로 실제 클립보드에 쓰고 끝에 비운다. 넣고 되읽은 픽셀이 같은지, 읽는 순서(PNG가 DIB보다 먼저), 글만 든 클립보드는 `none`인지, `CF_BITMAP`만 넣어도 `has_image`가 참인지, 둘째 블록이 실패하면 첫 블록도 남지 않는지(실패하는 블록을 주입한다).
- `tests/app_host_tests.cpp`(추가): 새 두 요청이 `post_clipboard_request`로 들어가 `take_clipboard_requests`로 나오는지.
- `luil_umbrella_header`(기존 검사): 새 헤더 둘이 `luil.h`에 있어야 통과한다.
- Android JNI 읽기는 `.cpp`를 묶은 확인 프로그램을 `run-as`로 실기기에서 돌린다(`platform_services_android`와 같은 길). 확인 프로그램이 넣은 글을 되읽고, 사람이 갤러리 앱에서 복사해 둔 그림을 읽어 `read_clipboard_data`의 꼴과 바이트 수를 적는다. Android 확인 프로그램은 초점이 없어 클립보드를 못 읽을 수 있다(실측). 막히면 모바일 데모의 label로 본다.
- 데모 `examples/demo/images_page.cpp`의 "클립보드" 줄: 단추 `그림 복사`(연 그림과 파일 이름 글, PNG는 logic thread에서 짓는다. 연 그림이 없거나 `clipboard_image_copy`가 거짓이면 꺼진다)·`그림 붙여넣기`, label `클립보드 그림: 있음`(`has_image`, 없으면 `클립보드 그림: 없음`, 첫 변화 알림 전에는 `클립보드 그림: 알림 전`), `붙여넣기 결과: 640×480 PNG`(DIB면 `… DIB`, 없으면 `붙여넣기 결과: 그림 없음`, 크면 `붙여넣기 결과: 너무 큼`, 풀지 못하면 `붙여넣기 결과: 풀지 못함 (이유)`), 넣기는 답이 없어 보낸 것을 적는 `그림 복사: 보냈다 (W×H, PNG n KB)`. 붙인 그림은 미리 보기 칸에 서고 상태 줄의 이름이 `클립보드 그림`이 된다. UIA는 label 이름을 되읽는다.
- 모바일 데모 `examples/mobile_demo/basics_page.cpp`의 그림 칸 아래: `클립보드 그림 붙여넣기` 단추(앱 UI 명령, 결정 13)와 `붙여넣기 결과:` label(`아직 없음`·`그림 없음`·`너무 큼`·`풀지 못함`, 풀었으면 `W×H PNG`처럼 크기와 앞 바이트로 가른 형식 이름 PNG·JPEG·GIF·WebP·DIB). `tests/mobile_demo_page_tests.cpp`에 사례를 더해 intent를 직접 주고 상태별 이름을 읽는다. `basics_page.cpp`를 `tests/CMakeLists.txt`의 모바일 데모 목록(`luil_core_tests`)에 올리는 일은 데모 연결 커밋이 한다. 이 브랜치에는 msg_box 연결([message-box-design.md](message-box-design.md))이 없어 그 커밋과 같은 등록 줄과 자산 대역(`tests/mobile_demo_assets_stub.cpp`)을 같은 글자로 넣었다. 통합이 두 벌을 하나로 합친다.
- 사람 확인: 그림판·브라우저·Office·메신저로 넣고 그쪽에서 복사한 그림을 붙이기, 투명 PNG의 바탕과 반투명 가장자리, 화면 캡처 도구의 그림, 큰 그림(수천만 화소)의 멈춤, 원격 데스크톱 클립보드. Android 실기기: 갤러리·키보드·메신저 앱에서 복사한 그림, 텍스트 칸에 그림 URI가 붙지 않는지, 시스템 알림.

## 범위 밖

- Android 그림 넣기와 클립보드 변화 알림(결정 7, Java 필요).
- HTML·RTF 형식(R3 rich_edit). 벡터 메타파일과 SVG 형식. 그림 파일 경로(`CF_HDROP`)를 그림으로 읽는 일은 파일 클립보드가 맡는다.
- 지연 렌더링과 넣기 결과를 앱에 돌려주는 답. 글·파일 복사처럼 결과가 없다.
- 움직이는 그림. 첫 장만 다룬다.
