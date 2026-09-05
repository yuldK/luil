# 자산 파이프라인

아이콘 글꼴과 accent 카탈로그의 배포 원본은 `assets/`에 두고, C++에서 사용하는 헤더는 빌드 시 생성한다. 원본과 생성물을 사람이 따로 편집하지 않으므로 이름과 코드포인트가 어긋나지 않는다.

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

대체 파일은 같은 스키마를 사용하고 기본 id `mint`를 포함해야 한다. 색 이름은 표시 문자열이므로 앱과 배포 환경의 언어에 맞는 카탈로그를 선택할 수 있다.
