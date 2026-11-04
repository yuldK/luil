# 차트 라이브러리

luil 저장소가 core 밖에 따로 세우는 선택 라이브러리 `luil_chart`의 꼴이다. CMake 옵션 `LUIL_BUILD_CHART`(기본 끔)를 켤 때만 저장소의 `chart/` 하위 프로젝트를 읽어 정적 라이브러리를 짓고 설치한다. 소비자는 `luil::luil_chart`를 링크하고 `#include "luil/chart.h"`로 들인다. `luil_core`·`luil` target과 [luil.h](../include/luil/luil.h)에는 들지 않는다. 요소의 그리기·키·접근은 [차트](chart-design.md)가 정한다. 차트 문서가 250줄을 넘어 라이브러리 꼴과 빌드를 이 문서로 나눴다.

## 문제

- 공개 헤더를 가진 선택 라이브러리를 세울 자리가 없다. 옵션은 두 갈래다. `LUIL_BUILD_TESTS`·`LUIL_BUILD_EXAMPLES`·`LUIL_BUILD_TOOLING`은 저장소 안 target을 더 짓는다([CMakeLists.txt](../CMakeLists.txt) 258~264행). `LUIL_ENABLE_*`는 luil 안 기능을 켜고, 끈 구성에도 공개 API가 남아 "지원 안 함"으로 답한다(300~303행).
- core 검사와 설치가 include 트리 전체를 본다. [verify_umbrella_header.cmake](../cmake/verify_umbrella_header.cmake) 21행은 `include/luil` 아래 모든 `.h`가 `luil.h`에 들어야 통과시킨다. [install.cmake](../cmake/install.cmake) 21행은 `include/`를 통째로 설치한다. 차트 헤더를 `include/luil/chart/`에 두면 "luil.h에 넣지 않는다"와 부딪히고, 차트를 끈 설치본에도 헤더만 들어간다.
- 두 소스 검사는 디렉터리를 스크립트 안에 박았다. [verify_default_text.cmake](../cmake/verify_default_text.cmake) 13~15행은 `src/ui`·`include/luil/ui`만, [verify_core_portability.cmake](../cmake/verify_core_portability.cmake) 14~24행은 core 디렉터리만 본다. 다른 루트를 넘길 길이 없어 core 밖 소스는 검사 밖이다.
- core 밖 코드는 core의 비공개 헤더를 쓸 수 없다. 설치본에는 `src/`가 없다. 기본 문구([default_text.h](../src/ui/default_text.h))와 검사기 이름표([inspect_names.cpp](../src/ui/inspect_names.cpp))가 비공개다. 기본 문구 규칙([default-text-design.md](default-text-design.md) 19·170·174행)과 kind 이름표 규칙([ui_element_id.h](../include/luil/ui/ui_element_id.h) 222행)을 core 밖에서 지킬 길을 정해야 한다.
- 데모 페이지는 core test에도 컴파일된다. [tests/CMakeLists.txt](../tests/CMakeLists.txt) 170~210행이 데스크톱·모바일 데모 페이지를 `luil_core_tests`에 넣고, 그 실행 파일은 `luil::luil_core`만 링크한다(218행). 데모가 차트를 쓰면 test 구성에도 차트가 있어야 한다.

## 공개 형식

```text
chart/                               # 하위 프로젝트. LUIL_BUILD_CHART가 켜질 때만 읽는다
  CMakeLists.txt                     # luil_chart target, 설치 규칙, 검사 test 등록
  include/luil/chart.h               # 우산 헤더. chart 공개 헤더 전부를 들인다
  include/luil/chart/chart_element.h
  include/luil/chart/chart_scale.h   # 요소 없는 순수 함수 (calendar_date.h 선례)
  src/chart_element.cpp
  src/chart_layout.h, chart_layout.cpp # 비공개 순수 계산(계열 표, 범주 자리, 칸, 이름표 솎기, 픽셀 열). test가 그대로 잰다
  src/chart_scale.cpp
  src/chart_text.h                   # 비공개 기본 문구. 낱말마다 함수
  src/chart_text.cpp                 # 표 조회. 코드 줄에 한글이 없다
  text/ko.json, text/en.json         # 문구 원본. 차트의 한글은 ko.json 한 곳
  tests/CMakeLists.txt               # luil_chart_tests
  tests/chart_scale_tests.cpp, chart_text_tests.cpp, chart_element_tests.cpp, chart_page_tests.cpp
```

```cmake
# 루트 CMakeLists.txt, LUIL_BUILD_TOOLING(264행) 뒤
option(LUIL_BUILD_CHART "Build the optional chart library (luil::luil_chart)." ${luil_chart_default})

# chart/CMakeLists.txt
add_library(luil_chart STATIC ${luil_chart_files})
add_library(luil::luil_chart ALIAS luil_chart)
target_include_directories(luil_chart
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(luil_chart PUBLIC luil_core luil_usage PRIVATE luil_options)

# 소비자
find_package(luil CONFIG REQUIRED COMPONENTS chart)  # 설치본. add_subdirectory 소비자는 LUIL_BUILD_CHART=ON
target_link_libraries(app PRIVATE luil::luil luil::luil_chart)
```

## 규칙

**경계.** 차트는 core의 공개 헤더만 쓴다. `luil/ui/ui_element.h`·`field_popup.h`·`accessibility.h`·`text_block.h`·`draw_primitives.h`와 `luil/theme/ui_theme.h`다. `src/` 아래 core 파일을 include하지 않아 설치본(`include/`·`lib/`)만으로 지어진다. 그래서 같은 코드를 저장소 밖 앱이 직접 짰어도 되는 꼴이고, core에는 공개 API를 넓히는 변경이 없다. test target `luil_chart_tests`만 `tests/core_platform_stub.cpp`가 core 비공개 헤더를 들여 `${LUIL_SOURCE_DIRECTORY}`를 PRIVATE include로 둔다. 라이브러리는 그대로 공개 헤더만 쓴다.

**링크 층.** `luil_core`만 PUBLIC으로 잇는다. 차트는 플랫폼을 모르는 요소라 Win32·Android 조립(`luil`)이 필요 없다. 그래서 Android의 core 프리셋(test만 짓는다)에서도 차트 test가 돈다. 앱은 어차피 `luil`을 링크한다. 내부 규율 `luil_options`(경고를 오류로, CMakeLists.txt 409~426행)는 PRIVATE, 소비자 요건 `luil_usage`는 PUBLIC이다(core와 같다).

**옵션 이름.** `LUIL_BUILD_CHART`다. `BUILD_*`는 target을 더하는 옵션이고, `ENABLE_*`는 끈 구성에도 API가 남는 기능 옵션이다. 차트는 끄면 target도 헤더도 없으므로 `BUILD_*` 쪽이다.

**기본값과 예제·test.** 기본은 끈다. 예제나 test를 켠 구성에서는 기본값이 켬을 따른다. 웹뷰의 기본값이 Direct3D를 따르는 선례(루트 `CMakeLists.txt`의 `luil_webview_default`)와 같은 꼴이다. 예제나 test를 켜고 차트를 명시적으로 끄면 configure가 원인과 함께 멈춘다(259~262행 선례). 옵션은 캐시에 값이 있으면 기본값을 다시 보지 않는다(279~280행). 차트를 끈 채 구성한 빌드 디렉터리에서 예제만 켜면 이 검사가 그 사실을 알린다.

**프리셋.** [CMakePresets.json](../CMakePresets.json)의 `vs2022-tests`·`vs2026-tests`·`vs2022-examples`·`vs2026-examples`·`android-arm64-core`의 `cacheVariables` 끝에 `"LUIL_BUILD_CHART": "ON"`을 둔다. `android-arm64`는 core 프리셋을 물려받는다. `vs2022`·`vs2026`·analysis 프리셋은 끈 구성의 확인 자리다.

**하위 프로젝트 읽기.** 루트가 `tests` 블록 뒤 파일 끝에서 `add_subdirectory(chart)`를 부른다. 예제가 먼저 읽혀도 `luil::luil_chart` 링크는 생성 때 풀린다. 차트 test는 `tests/`가 찾아 캐시에 둔 `LUIL_ADB_EXECUTABLE`([tests/CMakeLists.txt](../tests/CMakeLists.txt) 274행)을 써서 그 뒤라야 한다. 건너뜀 글(같은 파일 283행)은 디렉터리 범위 변수라 보이지 않아 `chart/tests`가 따로 정한다. IDE 폴더는 차트 CMake가 `luil_set_ide_folder("Libraries" luil_chart)`로 고른다.

**검사.** 차트 CMake가 CTest 셋을 등록한다. core의 세 검사처럼 Windows의 test 구성에서만 돈다. 이식성과 우산 헤더 두 스크립트의 선택 인자는 줄 1이 더한다. `CHECKED_DIRECTORIES`가 없으면 그 스크립트의 기본 목록이다(이식성은 core 목록, 한글 검사는 줄 4 뒤 `src`·`include` 전체). 한글 검사 스크립트는 줄 4가 소유하고, 줄 4의 검사 넓힘 커밋이 선택 인자 `CHECKED_DIRECTORIES`를 함께 들인다(아래 약속 절).

| test | 스크립트 | 넘기는 것 |
| --- | --- | --- |
| `luil_chart_default_text` | `verify_default_text.cmake` | 필수 인자 `SOURCE_DIRECTORY=chart/src`·`INCLUDE_DIRECTORY=chart/include`와 `CHECKED_DIRECTORIES`로 같은 두 디렉터리. 파일 예외는 없다(표 전환 뒤라 한글이 JSON에만 있다) |
| `luil_chart_portability` | `verify_core_portability.cmake` | 같은 세 인자. 필수 인자 둘은 위반 경로의 기준이기도 하다. 목록은 `add_test`에서 `$<SEMICOLON>`으로 잇는다 |
| `luil_chart_umbrella` | `verify_umbrella_header.cmake` | `INCLUDE_DIRECTORY=chart/include`, `UMBRELLA_HEADER=luil/chart.h` |

소스 스타일 검사는 저장소 전체를 돌고 `build`·`third_party`·`worktrees`만 뺀다([check_source_style.ps1](../scripts/check_source_style.ps1) 13행). `chart/`는 따로 할 일 없이 검사된다.

**기본 문구.** `chart/src/chart_text.h/.cpp`에 낱말마다 함수 하나를 둔다. core `default_text`와 같은 꼴이고 한국어 기본이며 OS 언어를 읽지 않는다(확정 G6 a). 차트의 한글은 이 문구 자리 한 곳이고 `luil_chart_default_text`가 지킨다. 함수는 `chart_name()`(차트), `chart_plot_name()`(자료 점), `chart_summary(series, categories, minimum, maximum)`("계열 2개, 범주 12개, 최솟값 3, 최댓값 120"), `chart_point_text(series_name, category, value)`("매출, 3월, 120", 계열이 하나면 이름 없이), `chart_missing_value()`(값 없음), `chart_empty()`(자료 없음), `chart_category_number(n)`("1"처럼 이름표가 없는 범주, 1부터 센 번호를 받는다)다. 수가 끼는 문장은 `std::u8string`을 돌려준다. 계열·범주의 수는 복수형 항목이 될 수라 수로 받고, 값의 글은 호출부가 지어 넘긴다(소수 자리와 `format_value`가 호출부 몫이다). 값 글이 비면(빈 점뿐인 자료) 요약의 값 부분을 뺀다. 라이브러리 커밋에서는 몸통이 한국어 글이다. 그 뒤 표 전환 커밋이 원본을 `chart/text/ko.json`·`en.json`으로 옮기고 몸통을 i18n 표 조회로 바꾼다(약속 절 줄 4). 함수 이름은 그대로이고, 표에서 찾는 함수는 고른 조회(`string_lookup`)를 첫 인자로 받는다. `text_languages()`가 잠금 아래에서 목록을 복사하므로 문구 함수마다 부르지 않고, 요소를 지을 때 `select_chart_strings()`로 한 번 고르기 때문이다([i18n-design.md](i18n-design.md) 215행). 그래서 호출부는 인자 하나가 늘었다. 언어가 바뀌면 앱이 다시 지은 요소부터 새 언어다. 키는 함수 이름이고 함수와 어긋나는 자리는 셋이다. 복수형은 `count` 하나로 고르므로 수가 둘인 요약은 복수형 항목 `chart_summary_series`·`chart_summary_categories`를 지어 문장 `chart_summary`(값 글이 비면 `chart_summary_without_range`)에 넣는다. 자리 값 문장에 갈래가 없으므로 계열 이름이 없는 점 글은 `chart_point_text_single_series`다. `chart_category_number`는 숫자 글자뿐이라 번역할 글이 없고 칸의 자동화 id에도 쓰여 언어에 따라 바뀌면 안 되므로 표에 두지 않는다. 그때부터 차트의 한글은 `ko.json`에만 있다. core의 `default_text.h/.cpp`를 고치지 않으므로, R5 i18n이 그 몸통을 바꾸는 일과 차트가 끝에 함수를 더하는 일의 병합 순서 문제가 없다.

**검사기 이름.** 결정 L2의 길 a면 core 이름표에 세 이름이 들어 앱이 할 일이 없다. 길 b면 앱이 `inspector_config::kind_names`([ui_inspect.h](../include/luil/ui/ui_inspect.h) 56행)에 차트 kind 이름을 넣는다.

**설치.** `LUIL_INSTALL`이 켜진 Windows 구성에서만이다. Android는 설치를 끈다(루트 `CMakeLists.txt`의 `LUIL_PLATFORM`이 android인 갈래). 차트 CMake가 자기 규칙을 낸다. `install(TARGETS luil_chart EXPORT luil-chart-targets ARCHIVE DESTINATION lib)`, `install(DIRECTORY chart/include/ DESTINATION include)`, `install(EXPORT luil-chart-targets NAMESPACE luil:: FILE luil-chart-targets.cmake DESTINATION lib/cmake/luil)`다. Debug 산출물에는 core처럼 접미사 `d`를 붙인다(install.cmake 15행). `luil_core`는 다른 내보내기 묶음(`luil-targets`)에 있고 CMake가 `luil::luil_core`로 참조를 잇는다. 설치 config는 `LUIL_BUILD_CHART`가 켜졌고 그 파일이 있으면 읽고 `luil_chart_FOUND`를 세운다. 그러면 [luil-config.cmake.in](../cmake/luil-config.cmake.in) 94행의 `check_required_components(luil)`가 `COMPONENTS chart`를 본다. 그 매크로는 찾지 못했다는 사실만 세우므로 차트를 요구받고 없을 때 `luil_NOT_FOUND_MESSAGE`에 원인을 적는다.

**판과 고지.** 판은 luil과 같다(`PROJECT_VERSION`). 차트는 제3자 코드를 들이지 않아 고지가 늘지 않는다.

**커밋 차례.** 라이브러리 커밋(`chart/`의 빌드 꼴·`chart_scale`·`chart_text`, 루트 옵션·하위 프로젝트 읽기, 프리셋, 설치 config와 설치 소비자, 두 스크립트 인자, 결정 L2의 core 줄), 요소 커밋(`chart_element`와 그 test, 설치 소비자의 `chart_element` 확인, [차트](chart-design.md)), 표 전환 커밋, 검사 등록 커밋, 데모 연결 커밋(예제 CMake, 두 데모 페이지, 모바일 페이지 목록과 그 test 숫자, 차트 페이지 test)을 나눈다. 줄 4 커밋과의 차례는 약속 절 줄 4에 적는다. 데모 연결은 통합 때 다른 줄의 데모 커밋 뒤에 직렬로 쌓는다.

## 결정

확정(2026-11-04): N5. 차트는 luil core(`luil_core`·`luil`과 `luil.h`)에 넣지 않고, 저장소가 제공하는 따로 선 선택 라이브러리로 둔다. CMake 옵션으로 켤 때만 빌드·설치하고 기본은 끈다. 저장소의 데모와 test 프리셋은 켜서 데모가 그 라이브러리를 참조한다. 켜지 않은 소비자 프로젝트는 지금과 같다.

확정(2026-11-04): 구현 시점. 사용자가 채팅에서 플래그로 켜는 조건부 라이브러리로 구현하라고 청했다. 메모의 "차후로 진행해도 돼"는 허용이었고, 계획대로 R5 안에서 구현한다.

확정(2026-11-02, 2026-11-04 재확인): G6 정책 a·범위 B. 기본 문구는 한국어 기본이고, i18n은 조회·자리 값·복수형만 공개한다. 숫자 서식은 없다.

**L1. 라이브러리의 자리.** 질문: 차트 소스와 헤더를 어디에 두는가.

- 길 a: 저장소 최상위의 하위 프로젝트 `chart/`(자기 `CMakeLists.txt`, `include/luil/chart/`, `src/`, `tests/`). 되는 것: 끄면 디렉터리를 읽지도 않는다. core의 우산 헤더 검사(21행)와 include 통째 설치(install.cmake 21행)에 걸리지 않는다. 헤더 경로는 `luil/` 접두를 지켜 설치본에서 core 헤더 곁에 선다. 대가: 최상위 디렉터리가 하나 늘고, 설치 규칙과 검사 등록을 차트 CMake가 한 벌 더 진다. 두 검사 스크립트에 선택 인자가 들고, 한글 검사의 인자는 줄 4에 기댄다.
- 길 b: core 트리 안 `include/luil/chart/`·`src/chart/`와 [src/CMakeLists.txt](../src/CMakeLists.txt)의 조건부 target. 되는 것: 소스 묶음 매크로와 검사 범위를 그대로 쓴다. 대가: 우산 헤더 검사가 차트 헤더를 `luil.h`에 요구해 예외 규칙을 더해야 하고, 설치가 include를 통째로 복사해 끈 설치본에도 차트 헤더가 들어간다. 공유 파일 수정이 a보다 많다.
- 길 c: 저장소 밖 별도 저장소. 되는 것: luil 저장소가 바뀌지 않는다. 대가: "luil에서 제공하는 표준 차트"라는 확정과 어긋나고, 데모가 바깥 저장소를 핀으로 받아야 한다.
- 권장: a. 대가: 위의 하위 프로젝트 한 벌. 승인: 길 a (2026-11-04).

**L2. 차트의 kind.** 질문: 차트 요소의 `ui_element_kind`를 누가 정하는가.

- 길 a: core enum에 `chart`·`chart_plot`·`chart_cell`을 예약한다. [ui_element_id.h](../include/luil/ui/ui_element_id.h) 217행 `page_transition_cover` 뒤, `end_of_library_kinds`(223행) 앞에 세 줄과 주석이고, [inspect_names.cpp](../src/ui/inspect_names.cpp) 238행 case 뒤에 이름 셋, [ui_element_tests.cpp](../tests/ui_element_tests.cpp) 84행 `last[]`에 `chart_cell`이다. 되는 것: 앱이 할 일이 없고, 검사기 이름과 자동화 id가 앱마다 같아 UIA 스크립트가 어느 앱에서나 같은 id로 찾는다. 웹뷰 kind가 웹뷰를 끈 구성에도 서 있는 선례(ui_element_id.h 91~93행)가 있다. 대가: core 헤더에 차트라는 이름이 들고, 차트를 끈 빌드에도 쓰지 않는 열거자 셋이 남는다(코드는 없다).
- 길 b: 앱이 `chart_config::kind`로 앱 kind(`application_element_kind(n)`)를 준다. 플롯과 칸은 같은 kind에 owner 꼬리(단위 구분자와 `plot`·칸 번호)로 가른다. 검사기 이름은 앱이 `kind_names`에 넣는다. 되는 것: core를 한 줄도 고치지 않는다. 대가: 쓰는 앱마다 kind를 고르고 이름표를 넣는다. 잊으면 kind가 `none`이라 구조 부품의 `{none, owner}`와 id가 겹칠 수 있다. 자동화 id가 앱마다 다르다.
- 길 c: 차트가 앱 대역의 끝 수를 고정해 쓴다. 되는 것: 열거자도 앱 kind 설정도 건드리지 않는다. 대가: 앱 대역은 앱의 것이라(ui_element_id.h 14~15행) 소비자 kind와 겹칠 수 있다. 권하지 않는다.
- 권장: a. 대가: 위의 core 다섯 줄 안팎과 남는 열거자. 승인: 길 a (2026-11-04).

**L3. 예제·test 구성에서 차트를 끄면.** 질문: 데모가 늘 차트를 쓰는데 예제나 test를 켜고 차트를 끈 구성은 어떻게 되는가.

- 길 a: 예제·test를 켜면 기본값이 켬이고, 명시적으로 끄면 configure 오류다. 되는 것: 데모 코드에 조건부 컴파일이 없고, 데모와 test가 늘 같은 페이지를 본다. 대가: 차트 없이 예제만 보려는 구성이 막힌다. 한 번 구성한 빌드 디렉터리는 캐시 값 때문에 오류로 원인을 듣는다.
- 길 b: 데모가 `LUIL_DEMO_CHART` 정의로 차트 페이지를 조건부로 싣고, 끄면 페이지 목록에서 뺀다. 되는 것: 어떤 구성이든 예제가 지어진다. 대가: 두 셸과 페이지 목록에 `#if`가 들고, 끈 구성의 데모를 따로 확인해야 한다. 사용자 메모의 "demo는 그 차트 라이브러리를 참조"보다 느슨하다.
- 권장: a. 대가: 위. 승인: 길 a (2026-11-04).

**L4. 기본 문구의 자리.** 질문: core 비공개 `default_text`를 쓸 수 없는 차트가 기본 이름과 요약을 어디에 두는가.

- 길 a: 차트 비공개 `chart_text.h/.cpp`(위 규칙). 되는 것: 경계를 지키고 core 문구 파일을 고치지 않는다. 같은 한글 검사를 차트 소스에 건다. 대가: 차트를 i18n 표에 올리는 일이 따로 든다(약속 절).
- 길 b: core `default_text.h/.cpp` 끝에 차트 함수를 더하고 차트가 core `src/`를 include한다. 되는 것: i18n이 core 표 하나로 차트까지 바꾼다. 대가: 설치본에는 그 헤더가 없어 경계가 깨지고, 차트를 끈 빌드에도 차트 문구가 core에 남는다. 같은 라운드 줄 4와 병합 순서가 묶인다.
- 길 c: 기본 문구 없이 앱이 이름과 요약을 반드시 준다. 되는 것: 문구 파일이 없다. 대가: 앱이 잊으면 접근 이름이 비고, 확정 G6 a(라이브러리 기본 문구는 한국어 기본)와 갈린다.
- 권장: a. 대가: 위. 권장대로 진행.

**L5. 설치와 find_package.** 질문: 설치본에서 차트를 어떻게 찾는가.

- 길 a: 구성 요소 `chart`. 따로 선 내보내기 파일(`luil-chart-targets.cmake`)과 `COMPONENTS chart`. 되는 것: 차트 없이 설치한 패키지에서 `COMPONENTS chart`가 원인과 함께 실패한다. 차트를 묻지 않는 소비자는 지금과 같다. 대가: 설치 config에 몇 줄이 든다.
- 길 b: 같은 내보내기 묶음(`luil-targets`)에 넣는다. 되는 것: 설치 config를 고치지 않는다. 대가: 차트가 있는지를 target 유무로만 알고, 묻는 길이 없다.
- 길 c: 설치하지 않고 `add_subdirectory` 소비자만 쓴다. 되는 것: 설치 config를 고치지 않는다. 대가: core의 두 소비 길(install.cmake 4~6행)과 갈린다.
- 권장: a. 대가: 위. 권장대로 진행.

## 플랫폼

**Win32.** 정적 라이브러리이고 설치 규칙이 선다. 플랫폼 코드는 없다.

**Android.** 같은 소스를 `luil_core`만 링크해 짓는다. Java 소스는 0줄이고 JNI도 없다. `android-arm64-core` 프리셋에서 `luil_chart_tests`가 실기기에서 돈다. `luil_text_shaping_tests`처럼 `CROSSCOMPILING_EMULATOR`에 adb 실행기를 걸고 `catch_discover_tests`를 test 시점으로 미룬다(tests/CMakeLists.txt 317~321행 선례). `include(Catch)`와 건너뜀 글은 `chart/tests/CMakeLists.txt`가 직접 둔다(글은 디렉터리 범위 변수다). 모바일 데모의 공유 라이브러리가 `luil_chart`를 링크한다. 설치는 없다. Android의 luil에는 접근성 계층이 없다.

## 다른 노드와의 약속

**core에 닿는 줄.** 라이브러리 커밋이 고친다. 모두 끝에 덧붙이거나 정한 자리 한 곳이다.

| 파일 | 자리 | 무엇 |
| --- | --- | --- |
| `CMakeLists.txt` | 264행 뒤 | 기본값 계산, `option(LUIL_BUILD_CHART ...)`, 예제·test를 켜고 끈 경우의 오류 |
| `CMakeLists.txt` | 파일 끝(489행 뒤) | `if(LUIL_BUILD_CHART) add_subdirectory(chart) endif()` |
| `CMakePresets.json` | 다섯 프리셋의 `cacheVariables` 끝 | `"LUIL_BUILD_CHART": "ON"` |
| `cmake/luil-config.cmake.in` | 39행 뒤, 62행 뒤 | `LUIL_BUILD_CHART` 값, 차트 내보내기 파일을 읽고 `luil_chart_FOUND`, 없을 때의 원인 글 |
| `cmake/verify_core_portability.cmake` | 14행 | 선택 인자 `CHECKED_DIRECTORIES`(없으면 core 목록) |
| `cmake/verify_umbrella_header.cmake` | 15·24행 | 선택 인자 `UMBRELLA_HEADER`(기본 `luil/luil.h`). 24행에서 자기 자신을 건너뛰는 이름도 이 값과 견준다 |
| `include/luil/ui/ui_element_id.h` | 217행 뒤 | 결정 L2 길 a의 세 kind |
| `src/ui/inspect_names.cpp` | 238행 case 뒤 | 이름 셋 |
| `tests/ui_element_tests.cpp` | 84행 | `last[]`에 `chart_cell` |
| `tests/package_consumer/CMakeLists.txt` | 4행과 끝 | 4행을 `find_package(luil CONFIG REQUIRED OPTIONAL_COMPONENTS chart)`로 바꾼다. 끝에서 `LUIL_BUILD_CHART`가 참이면 `luil_chart_FOUND`를 요구하고 `luil::luil_chart`를 링크한다. `LUIL_CONSUMER_CHART`는 SVG·Lottie 정의처럼 늘 `LUIL_BUILD_CHART`의 참거짓으로 정의한다 |
| `tests/package_consumer/main.cpp` | include 뒤, `LUIL_CONSUMER_LOTTIE` 확인(65행) 뒤 | `#if LUIL_CONSUMER_CHART`로 `luil/chart.h`를 들이고 `nice_ticks`·`format_chart_number`를 부른다. 요소 커밋이 `chart_element` 하나를 더 짓는다 |

설치 소비자의 차례. `LUIL_BUILD_CHART`는 첫 `find_package`가 설치 config를 읽은 뒤에야 알려진다. 그래서 찾기를 한 번만 하고 구성 요소를 선택으로 묻는다. 차트를 끈 설치본에서는 `luil_chart_FOUND`가 거짓이어도 통과하고, 켠 설치본에서는 끝의 검사가 내보내기 파일이 실제로 읽혔는지 본다. `find_package`를 `COMPONENTS chart`로 한 번 더 부르는 꼴은 쓰지 않는다. 설치 config가 부를 때마다 `luil_find_skia()`를 다시 돌고(luil-config.cmake.in 41행) 그 함수의 `add_library(luil_skia INTERFACE)`([skia.cmake](../cmake/dependencies/skia.cmake) 275행)에 이미 선 target을 거르는 검사가 없어 둘째 호출이 configure 오류가 된다. 줄 4가 설치본 표 확인으로 같은 파일 끝에 줄을 덧붙일 수 있다. 그러면 통합 때 줄 4 커밋 뒤에 차트 블록을 쌓는다(아래 차례와 같다).

데모 연결 커밋은 [examples/CMakeLists.txt](../examples/CMakeLists.txt) 153·182행의 링크와 데모 소스 목록, [mobile_demo.cmake](../examples/mobile_demo/mobile_demo.cmake)의 소스 목록(공통 견본 `examples/demo/chart_sample.*`이 두 목록에 함께 든다), [examples/android/CMakeLists.txt](../examples/android/CMakeLists.txt) 54행 뒤 `target_link_libraries(luil_mobile_demo PRIVATE luil::luil_chart)`, [page_list.cpp](../examples/mobile_demo/page_list.cpp) 22행 `std::array<page_entry, 14>`를 15로 늘리고 차트 항목 한 줄, [mobile_demo_page_list_tests.cpp](../tests/mobile_demo_page_list_tests.cpp) 46행 `14u`를 `15u`로 바꾸는 것이다. `tests/CMakeLists.txt`는 고치지 않지만 이 test 파일의 단언 숫자는 고친다. `page_list.cpp`는 [luil_core_tests](../tests/CMakeLists.txt)에 컴파일된다(183행). 페이지 키는 `page_files`처럼 페이지 헤더가 든다([files_page.h](../examples/mobile_demo/files_page.h) 23행, page_list.cpp 7행이 include한다). 그래서 `page_chart`를 든 `examples/mobile_demo/chart_page.h`와 그 헤더가 들이는 `examples/demo/chart_sample.h`는 `luil/chart.h`를 include하지 않고, 차트 include는 `chart_sample.cpp`에만 둔다. `page_list.cpp`는 이름표 배열만 바뀌어 `luil_core_tests`가 차트 include 경로도 링크도 요구하지 않는다. 차트 페이지 자체는 core test에 넣지 않고, 그 test는 `luil_chart_tests`에 든다.

- **줄 4(i18n).** 차트는 i18n의 공개 API만 쓴다. 그 문서가 이미 정했다. `luil_add_string_table`은 설치되는 CMake 함수라 아무 target에나 걸리고([i18n-design.md](i18n-design.md) 152~153행), `select_strings`는 어떤 `string_table`이든 받는다(같은 문서 73행). 함수는 줄 4가 루트 `CMakeLists.txt` 446행 뒤에 넣는 `string_table.cmake` include로 서고, 그 자리가 파일 끝의 `add_subdirectory(chart)`보다 앞이라 차트 CMake에서 부를 수 있다.
  - 표 전환. 차트는 `chart/text/ko.json`·`en.json`을 두고 `chart/CMakeLists.txt`에서 `luil_add_string_table(luil_chart NAME chart_strings DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/text" BASE ko)`를 부른다. `chart_text.cpp` 몸통은 `select_strings(chart_strings::table, luil::text_languages().value_or(std::vector<std::u8string> {}))`가 낸 조회에서 키를 찾는다(i18n-design.md 107행 꼴). 조회는 요소를 지을 때 한 번 고른다(기본 문구 규칙). 함수 이름이 키 이름이다(default-text-design.md 174행과 같은 규칙). 기본 언어가 `ko`라 확정 G6 a가 그대로다. 설치본에는 원본도 생성 헤더도 실리지 않는다. 표는 정적 라이브러리에 들어 설치 소비자는 표를 짓지 않는다. 이 커밋은 줄 1의 일이고 통합 때 쌓았다.
  - 한글 검사. `verify_default_text.cmake`는 줄 4가 소유하고 줄 1은 이 파일을 고치지 않는다. 줄 4의 검사 넓힘 커밋이 선택 인자 `CHECKED_DIRECTORIES`(없으면 `src`·`include` 전체)를 함께 들이고 `default_text.cpp` 파일 예외를 선택 인자 `TEXT_FILE`로 바꿨다. `luil_chart_default_text`는 `chart/src`·`chart/include`만 넘기는 등록이다. 표 전환 뒤라 문구 파일 예외 인자는 필요 없다. 차트를 끈 구성에서 core 검사가 `chart/`를 보지 않게 하려고 core 목록에 넣지 않고 인자로 넘기며, [i18n-design.md](i18n-design.md) 215행과 흡수 문서도 같은 꼴로 맞췄다.
  - 통합 차례. 줄 4 표·흡수 커밋, 줄 1 차트 라이브러리·요소 커밋, 줄 1 표 전환 커밋, 줄 4 검사 넓힘 커밋, 줄 1 검사 등록 커밋이다. 넓힘이 표 전환보다 앞서면 라이브러리 커밋의 한국어 몸통이 걸리므로 이 차례를 지킨다.
- **줄 2(hot_reload).** 차트는 스타일 키를 더하지 않아 다시 읽기와 만나지 않는다. 줄 2는 루트 CMake를 고치지 않고 줄 4는 76행·446행 뒤다. 차트의 264행 뒤·파일 끝과 겹치지 않는다. 프리셋은 같은 블록 끝에 여러 줄이 더할 수 있어 통합 때 줄 단위로 합친다.
- **줄 3.** 겹치는 파일이 없다. controller를 고치지 않는다.

## 확인

- `luil_chart_default_text`·`luil_chart_portability`·`luil_chart_umbrella`(CTest, `cmake -P`). 일부러 `chart_element.cpp` 코드 줄에 한글을 넣으면 첫째가, `<windows.h>`를 넣으면 둘째가, 새 공개 헤더를 우산 헤더에 빠뜨리면 셋째가 실패하는 것을 한 번 확인한다. 첫째는 검사 등록 커밋 뒤라 `chart_text.cpp`도 예외 없이 통과해야 한다. core의 세 test(`luil_default_text`·`luil_core_portability`·`luil_umbrella_header`)는 인자 없이 기본 목록으로 통과한다(한글 검사의 기본 목록은 줄 4 넓힘 뒤 `src`·`include` 전체다).
- 끈 구성: `vs2026` 프리셋으로 구성하면 `luil_chart` target이 없고 `chart/`를 읽지 않는다. 예제를 켜고 `-DLUIL_BUILD_CHART=OFF`를 주면 configure가 원인 글로 멈춘다.
- `luil_installed_consumer`(`vs2026-tests`): 차트를 켠 설치본에서 소비자가 `OPTIONAL_COMPONENTS chart`로 한 번 찾고 `luil_chart_FOUND`를 요구한 뒤 `nice_ticks`·`format_chart_number`를 부르고(요소 커밋부터 `chart_element` 하나를 더 짓고) 링크한다. 차트를 끈 설치본에서는 같은 소비자가 차트 없이 지어진다. 설치본에 `include/luil/chart.h`와 지은 구성의 `.lib`(Debug면 `lib/luil_chartd.lib`)가 있다. 그 test는 한 구성만 설치한다.
- Android: `android-arm64-core`에서 `luil_chart_tests`가 실기기에서 돈다. `android-arm64`에서 모바일 데모 APK가 지어진다.
- 요소와 데모의 test, UIA 되읽기, 사람 확인은 [차트](chart-design.md)에 있다.

**이 PC 흔적.** 이 문서의 확인이 남기는 것이다.

| 무엇 | 자리 | 지우는 법 |
| --- | --- | --- |
| 빌드 산출물 | 각 프리셋의 `build/<프리셋>/chart/` | 빌드 디렉터리째 지운다 |
| 설치 소비자 | `build/vs2026-tests/package-consumer-<구성>/`(설치본, 옮긴 사본, 소비자 빌드) | test가 다시 돌 때 덮는다. 디렉터리째 지워도 된다 |
| 기기 | adb 실행기가 올린 `/data/local/tmp/luil/luil_chart_tests` | 다음 실행이 덮는다. `adb shell rm -r /data/local/tmp/luil` |
| 레지스트리·사용자 폴더 | 없다 | |

## 범위 밖

- 공유 라이브러리(DLL·.so)로 짓기. core와 같은 정적 라이브러리다.
- 둘째 선택 라이브러리를 위한 일반 틀(하위 프로젝트 목록 돌기). 둘째가 생기면 그때 정한다.
- 패키지 관리자 포장과 따로 선 판 번호.
- core 팔레트의 계열색 역할. [차트](chart-design.md) 결정 E2가 정한다.
