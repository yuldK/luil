# 글꼴과 아이콘 글리프

`luil/text/fonts.h` 또는 `luil/luil.h`를 포함하면 공개 API를 사용할 수 있다.

```cpp
const auto families = luil::installed_font_families();
luil::set_ui_typeface_families({ u8"Pretendard", u8"Malgun Gothic", u8"Segoe UI" });
```

목록은 DirectWrite에서 읽은 UTF-8 가족 이름이며 정렬과 중복 제거를 거친다. 조회에 실패하면 빈 목록을 반환한다. 기본 UI 글꼴은 앞에서부터 설치된 가족을 선택하며, 빈 목록이나 사용할 수 없는 이름만 지정하면 Segoe UI를 사용한다. 앱 시작 전에 설정한다. 호출 시 내부 글꼴 캐시를 비우지만 이미 만들어진 화면과 글꼴 객체를 갱신하지는 않는다.

## 아이콘 폰트

`glyph_element`는 폰트의 Unicode 코드포인트 하나를 가운데 정렬해 그린다. 폰트를 생략하면 실행 파일에 내장된 Codicons를 사용한다.

```cpp
#include <luil/luil.h>
#include <luil/generated/codicons.h>
#include <memory>

auto icon = std::make_unique<luil::glyph_element>(
    luil::ui_element_id { luil::application_element_kind(0) },
    luil::glyph_config {
        .glyph = luil::codicons::icon_search,
        .font_size = 20.0f,
        .description = u8"검색",
    });
```

컨테이너에서 아이콘의 칸을 배정한다. `font_size`는 논리 픽셀이며 화면 배율이 적용된다. 글리프의 실제 윤곽을 칸 중앙에 놓고 칸 밖은 잘라낸다. 색을 생략하면 테마의 `primary_foreground`를 사용하며 `.color = luil::make_ui_color(80, 160, 240)`으로 지정할 수 있다. `description`이 비어 있으면 장식이고, 설명이 있으면 보조 기술에 이미지로 노출한다.

다른 TTF/OTF 아이콘 폰트는 시스템에 설치하지 않고 파일이나 바이트에서 읽는다. 반환된 `sk_sp<SkTypeface>`는 원본 데이터를 소유하고 복사 시 참조를 공유하므로, 앱에서 한 번 읽어 보관한 뒤 여러 element에 전달한다.

```cpp
const auto icons = luil::load_typeface_file(std::filesystem::path { u8"assets/내아이콘.ttf" });
if (icons != nullptr)
{
    luil::glyph_config config {
        .glyph = U'\uE001', // 해당 폰트의 매핑에 있는 Unicode 코드포인트
        .font_size = 24.0f,
        .typeface = icons,
    };
    // 이 config로 glyph_element를 만든다.
}
```

`load_typeface_bytes(std::span<const std::uint8_t>, face_index)`는 메모리의 폰트를 읽는다. 파일 로더도 선택적인 `face_index`를 받으며 기본값은 0이다. 읽기 실패·손상된 데이터·잘못된 face index는 `nullptr`를 반환한다. 로딩 성공 여부를 확인한 뒤 전달한다. `glyph_config::typeface == nullptr`는 내장 Codicons를 선택한다는 뜻이다.

`glyph`는 폰트 내부 글리프 색인이 아니다. 0이나 잘못된 Unicode 값, 지정한 폰트에 없는 문자는 그리지 않는다. 아이콘의 의미가 달라질 수 있으므로 시스템 폰트 대체는 하지 않는다.

직접 그리는 element에서는 기존 `draw_centered_glyph`와 `SkFont`를 사용할 수 있다. 내장 폰트도 공개 함수 `luil::load_codicon_typeface()`로 얻을 수 있다. 일반 텍스트처럼 기준선과 글자 전진 폭으로 그리려면 이 폰트로 `SkFont`를 만들고 기존 `draw_text`/`measure_text`에 UTF-8 문자열을 전달한다. 이 일반 텍스트 경로는 기존의 폰트 대체 규칙을 따른다.

드래그의 기본 대상 강조를 앱에서 직접 그리려면 payload의 `suppress_drop_highlight`를 `true`로 설정한다. 대상 테두리와 목록·탭의 drop 배경이 사라지고 hit test와 drop 액션은 유지된다. ghost는 별도의 `custom_visual`로 제어한다.
