# 자산 파이프라인

아이콘 글꼴과 accent 카탈로그의 배포 원본은 [`assets/`](../../assets/)에 두고, C++에서 사용하는 헤더는 빌드 시 생성한다. 원본과 생성물을 사람이 따로 편집하지 않으므로 이름과 코드포인트가 어긋나지 않는다.

## Codicons

`assets/codicons/mapping.json`은 이름과 코드포인트의 원본이다. `cmake/generate_codicons.cmake`가 이를 다음 형태의 헤더로 변환한다.

```text
assets/codicons/mapping.json
    ↓
generated/include/luil/generated/codicons.h
    inline constexpr char32_t icon_close = …;
```

생성기는 JSON 이름을 C++ 식별자로 정규화하고 충돌을 발견하면 헤더 생성 작업을 실패시킨다. `codicon.ttf`, mapping, 라이선스 파일은 `cmake/verify_codicons.cmake`의 SHA-256 검증도 통과해야 한다. 검증은 라이브러리 빌드 전과 `luil_assets_checksum` 테스트에서 수행된다. Codicons 디렉터리는 `.gitattributes`에서 줄 끝 변환을 제외해 바이트 해시를 보존한다.

생성한 헤더는 `luil::codicons` namespace의 상수를 제공한다. 앱은 생성물을 직접 편집하지 않고 mapping의 alias를 사용한다.

## Accent 카탈로그

`assets/accents.json`은 `cmake/generate_accents.cmake`가 `accents.h`로 만든다. 소비자는 configure 때 카탈로그를 바꿀 수 있다.

```cmake
set(LUIL_ACCENT_CATALOG "${PROJECT_SOURCE_DIR}/assets/accents.json"
    CACHE FILEPATH "Accent catalog JSON embedded at build time.")
```

대체 파일은 같은 스키마의 JSON 배열을 사용한다. 파일 이름은 `accent.json` 등 자유롭게 지정할 수 있다. 기본 id는 `mint`이며 `LUIL_DEFAULT_ACCENT_ID`로 바꿀 수 있다. 지정한 기본 id는 카탈로그에 반드시 있어야 한다. `system`은 OS accent용 예약 id다.

```cmake
# add_subdirectory(luil) 전에 설정한다. 기존 캐시에는 -D 옵션으로 변경한다.
set(LUIL_ACCENT_CATALOG "${CMAKE_CURRENT_SOURCE_DIR}/accent.json" CACHE FILEPATH "Accent catalog")
set(LUIL_DEFAULT_ACCENT_ID "brand" CACHE STRING "Default accent")
add_subdirectory(luil)
```

각 항목에는 `id`, `label`, `swatch`, `dark`, `light`가 필요하다. 두 테마 각각 `accent`, `accentHover`, `accentSoft`, `accentEmphasisFg`를 `#rrggbb`로 지정한다. 기본 파일을 복사해 수정하면 된다. 알 수 없는 id의 fallback과 `appearance_settings`의 초기값에 같은 기본 id를 사용한다. 표시 이름에는 UTF-8과 따옴표·줄바꿈·역슬래시를 사용할 수 있다.

카탈로그는 빌드 시 내장된다. JSON 변경 후 라이브러리를 다시 빌드해야 하며 설치된 바이너리의 JSON을 실행 중 읽는 기능은 아니다.
