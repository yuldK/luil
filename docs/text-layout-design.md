# UI 텍스트 레이아웃 설계안

상태: **사용자 승인 후 X8 구현 연결**. 아래 설계와 마지막 절의 실제 API·검증 범위를 함께 본다.
[인계 문서 X8](ui-gap-codex-track.md#x8-ui-텍스트-shaping-장기)의 선행 검토 절차를 따른다.

## 문제와 목표

도입 전 UI의 `draw_text`는 글꼴 fallback run을 `drawSimpleText`로 그리고,
`measure_text`는 같은 글꼴의 `measureText`를 더했다. SVG·Lottie에 연결된
`text_shaping_factory()`는 UI 컨트롤의 측정·caret 경로에 연결되어 있지 않았다.
글자소 경계(X1)는 이모지·결합 부호 내부로 커서가 들어가지 않게 하지만
합자·문맥형 글리프의 실제 폭과 caret 위치를 일치시키지는 못한다.

하나의 불변 `text_layout` 결과를 측정, 그리기, hit test, caret, 선택,
줄 넘김에서 공유한다. 앱의 편집 상태와 TSF·Android·UIA의 원본 offset은 계속
UTF-8 byte다. 글리프 번호나 UTF-16 offset을 공개 편집 상태로 올리지 않는다.

## 값과 수명

공개 `text_layout`은 복사 비용이 작은 값으로 두고 불변 구현을 공유한다.
Skia 타입은 비공개 구현에 둔다. 다음 정보는 같은 결과에서 나온다.

| 정보 | 저장할 내용 | 사용하는 곳 |
| --- | --- | --- |
| 원문 | 소유하는 UTF-8 텍스트와 revision | offset 검증, 입력·접근성 |
| 글리프 run | typeface 참조, glyph ID, 위치, advance, 원문 범위, 방향 | 그리기 |
| cluster | 원문 byte 범위, run/glyph 범위, 시각 경계 | hit test, caret |
| 줄 | 원문 범위, run 목록, baseline, ascent/descent, 논리·잉크 bounds | 배치, 선택, 스크롤 |
| caret stop | X1 글자소 경계, 위치와 upstream/downstream affinity | 입력·IME·UIA |

공개 API는 `include/luil/ui/text_layout.h`에 있다. 아래 형태를 구현하고 측정기 주입 overload를 함께 제공한다.

```cpp
struct text_layout_request;
class text_layout;
text_layout make_text_layout(text_layout_request request);
// text_layout::size(), lines(), caret_rect(offset, affinity)
// text_layout::hit_test(point), selection_rects(begin, end)
// draw_text_layout(canvas, layout, origin, paints)
```

request는 소유 텍스트, UI/code 글꼴, 크기, 폭/줄 넘김 정책, 배율,
폰트 설정 revision과 메트릭에 영향을 주는 스타일 run을 담는다.
색·검색·선택 바탕은 레이아웃을 무효화하지 않는다. 굵기·글꼴·크기는 무효화한다.
문서 장식 offset은 유효한 글자소 경계이며 반열림 범위다.

## 생성과 일관성

1. 문단을 나누고 X1 글자소 경계를 구한다. CRLF와 마지막 빈 줄의 기존 의미를 보존한다.
2. 기존 `text_shaping_factory()`로 shaper를 만든다. HarfBuzz/SkUnicode의 결과에서
   UTF-8 cluster offset, run 방향과 glyph 위치를 수집한다. UI용 별도 HarfBuzz 공장을 만들지 않는다.
3. 기본 글꼴에 없는 글자는 문맥을 보존하는 fallback run으로 shape한다. 기존의 코드포인트별
   `split_text_runs`를 그대로 shaping 전처리로 쓰면 결합 부호·ZWJ·스크립트 문맥을 끊을 수 있다.
   문단 run iterator의 fallback을 이용하고 결합 시퀀스는 가능한 한 같은 face에서 처리한다.
4. 첫 단계의 줄 넘김은 현재 공백 우선·글자소 fallback 정책을 보존한다. 후보 폭을 shaped
   advance로 고르고, 줄 경계에서 문맥이 바뀌는 run은 다시 shape한다. 최종 glyph 위치와
   최종 caret 표는 이 결과에서 함께 만든다. prefix를 따로 재측정하지 않는다.
5. 측정과 draw는 동일한 결과의 advance와 baseline을 사용한다. caret stop은 원본 글자소에만 둔다.
   하나의 합자가 여러 글자소를 덮으면 글꼴의 ligature caret 정보가 있으면 사용하고,
   없으면 cluster advance를 글자소 수로 분할한다. 해당 fallback은 테스트와 API 문서에 명시한다.
6. 선택은 줄별 사각형 목록을 반환한다. 줄 바꿈 선택과 upstream caret은 기존 동작을 보존한다.
   장식 바탕 → 검색 바탕 → 선택 바탕 → glyph → IME 밑줄 → caret 순서로 그린다.

shaping 결과에는 방향 정보를 저장한다. 초기 배포 범위는 LTR 문단과 결합·합자의 일관성이다.
RTL 글리프를 그릴 수 있다는 이유로 양방향 편집 지원을 선언하지 않는다. 혼합 방향의 시각 이동,
경계 affinity, 선택의 분리 사각형 규칙은 후속 단계에서 확정한다. UAX #14 줄 나눔도 별도 단계다.

## 옵션과 실패 처리

`LUIL_ENABLE_TEXT_SHAPING=OFF`에서는 현재 `drawSimpleText`·측정 경로를 유지한다.
공유 layout의 primitive 구현을 위해 끈 구성의 glyph·폭을 바꾸지 않는다. X1 경계 보강은 유지한다.
켜진 구성은 기존 공장의 플랫폼 ICU → 설정된 fallback → primitive 정책을 재사용한다.
실제 backend를 진단값으로 제공하여 fallback을 shaping 성공으로 오인하지 않게 한다.
동일 layout을 그리는 도중 다른 backend로 바꾸지 않는다. OS별 링크 대상·고지는 기존 옵션을 따른다.

## 캐시와 대용량 문서

배치 thread가 문단 layout을 만들고 게시한 뒤 render/input thread는 읽기만 한다.
Skia canvas·mutable shaper·break iterator를 결과에 공유하지 않는다. typeface와 glyph buffer만
불변 참조로 공유한다. 임의 앱 측정기와 기본 layout은 캐시를 섞지 않는다.

키는 문단 텍스트/revision, 글꼴 revision·크기·face, 폭, 배율, 스타일의 메트릭 revision,
backend다. 내용 hash만으로 동일성을 단정하지 않고 원문도 비교한다. X2의 문단 캐시를
layout 캐시로 확장하고 X5의 가시 범위 구간 질의는 유지한다. 캐시는 byte 예산과 LRU 퇴출을
가진다. 삭제한 문단을 영구 보관하지 않는다. 20MB 문서에서 caret만 바뀔 때 전체 shape를
다시 하지 않는다. 고정 여백 폭 변경은 줄 넘김 폭을 바꿀 때만 무효화한다.

## 도입 순서와 검증

1. 불변 값과 primitive 구현, 기존 측정기와의 어댑터를 추가한다. 옵션 OFF의 기존 raster 결과를 비교한다.
2. 옵션 ON의 HarfBuzz backend를 연결한다. 라벨과 버튼부터 같은 결과로 측정·그리기한다.
3. 한 줄 입력의 hit test·caret·선택·IME 위치를 연결한다. 이어 문서 보기의 줄 넘김·여백·장식을 옮긴다.
4. 컨트롤별 전환이 끝난 뒤 기존 prefix 측정 경로를 정리한다. UAX #14와 양방향 편집은 별도 변경으로 낸다.

검증은 `fi/ffi`, 라틴 결합 부호, 조합형 한글, 피부색/가족/국기 이모지, font fallback,
빈 줄·CRLF, wrap 양쪽 affinity, 검색/선택 겹침, 큰 글꼴·DPI 변경을 포함한다.
각 caret stop에 대해 offset → 위치 → hit test의 왕복을 확인하고 같은 위치의 affinity는 따로 비교한다.
Win32 TSF 후보 창·UIA 범위와 Android IME 좌표를 실제 플랫폼에서 확인한다.
20MB 문서의 최초 배치와 문단 한 곳 수정·스크롤·caret 이동을 따로 계측한다.

사용자가 승인한 불변 소유권, 합자 caret 균등 분할 fallback, 초기 LTR 범위와
후속 UAX #14·양방향 편집 분리 기준으로 구현했다.

## 구현된 API와 연결

```cpp
luil::text_layout_request request {};
request.text = u8"office é";
request.font_size = 16.0f; // 물리 픽셀이다.
request.width = 240.0f;
request.wrap = true;
const auto layout = luil::make_text_layout(std::move(request));
const auto caret = layout.caret_rect(6);
const auto hit = layout.hit_test({ caret.x, caret.y + 1.0f });
const auto selection = layout.selection_rects(0, 6);
```

값은 원문·줄·cluster·caret stop·run 정보를 읽기 전용으로 내준다. Glyph·위치·typeface는
native 불변 payload에 있다. 줄은 baseline·ascent/descent·논리 크기·ink bounds를 가진다.
`backend()`는 unavailable·measured·primitive·harfbuzz를 구별한다. 글자가 하나도 없는 typeface(SkFont가
nullptr 대신 넣는 빈 typeface)는 unavailable이라 입력 칸·문서 보기가 자기 측정기로 돌아간다.
shaper나 run iterator를 만들지 못하면 예외 대신 배치 전체를 primitive로 다시 짓고 그 backend를 알린다.
잘못된 UTF-8은 고친 사본으로 shape하지 않고 원문 byte offset 그대로 primitive로 잰다. 옵션 ON의
`measure_text`·`draw_text`도 원문을 넘겨 같은 글의 폭이 배치와 같다.
접은 줄 끝의 공백은 그 줄의 범위와 caret stop에 남지만 줄 폭과 `size()`에는 들어가지 않는다.
접지 않은 마지막 줄의 공백은 폭에 남는다. 자르는 자리는 언제나 글자소 경계다.
`remake_text_layout`은 기준 배치의 글꼴을 빌리되 요청한 `font_size`로 다시 잰다.
`approximate_ligature_carets()`는 SkShaper가 OpenType ligature caret 표를 내주지 않아
글자소 수로 분할한 경우를 알린다. UTF-8 byte offset과 soft-wrap affinity를 계속 쓴다.
`draw_text_layout`의 구간도 유효한 글자소 경계다. 부분 합자는 같은 glyph의 해당 영역을 잘라 칠한다.

옵션 ON의 공용 `measure_text`·`draw_text`는 같은 native cache를 사용하므로 라벨·버튼 등도
shaping 결과의 폭과 glyph로 그린다. 입력 칸은 배치한 값을 그리기·hit test·선택·IME 범위에 공유한다.
문서 보기는 문단의 접은 자리·폭·굵기 구간을 기존 cache에 보관하고, 가시 줄의 최종 glyph 값을
배치 시점에 보유한다. 조회한 문서가 조합 문서와 달라도 같은 주 글꼴 참조로 다시 배치한다.
사용자 측정기를 주입한 기존 컨트롤은 기존 경로를 유지한다. 공개 측정기 overload는 별도 기하 값만
만들며 native glyph·전역 cache를 사용하지 않는다.

색·검색·선택은 cache key에서 제외한다. 굵기는 key에 들어간다. Native cache는 프로세스당
**16MiB·512항목 상한의 LRU**이며 `text_layout_cache_usage()`로 보유량·hit/miss를 본다.
byte 수는 원문·기하·glyph buffer와 cache key·색인의 보유량이며 Skia/OS의 공용 글꼴 cache는 제외한다.
항목은 revision·글꼴·key·원문의 hash로 찾고, 같은 hash의 항목은 모두 끝까지 비교해 충돌이 다른 배치를
돌려주지 않는다. 원문은 배치가 가진 사본과 비교하므로 key에 따로 두지 않는다. 큰 항목은 LRU에 넣지 않는다. key는 넣기 전에 맞춘다 — NaN·무한 폭과 줄 높이는 0, 접지 않는 요청의
폭은 0이고, 글꼴이 정한 face는 key에서 뺀다. 지난 revision의 항목은 revision이 오른 뒤 처음 볼 때
거두고, 자물쇠 밖에서 짓는 사이 같은 요청이 먼저 들어왔으면 그 값을 돌려준다. `text_area_wrap_cache`는 현재 문서의 접은 자리만 남기므로,
큰 문단도 caret만 바뀌었다고 전체 문단을 다시 shape하지 않는다. 이 문단 cache를 앱이 유지해야 한다.
`text_layout_revision()`은 글꼴·언어·fallback·그리기 품질 변경 때 증가한다.
발행된 레이아웃의 글꼴·glyph는 계속 살아 있고 다음 배치부터 새 설정을 쓴다.

새 `grapheme_boundaries()`는 전체 경계를 iterator 한 번으로 수집한다. 기존 단일 경계를 반복 조회해
플랫폼 iterator가 긴 문단의 처음부터 다시 걷는 비용을 피한다. 기본 경계 표와 플랫폼 경계는 기존 규칙을 따른다.

## 검증 범위

`text_layout_tests.cpp`는 원문 소유권, 글자소 왕복, 합자 cluster의 caret 분할, 줄 경계 재배치,
CRLF·빈 줄, 선택, 동일 x 경계와 동시 읽기를 확인한다. 합자 기하 시험의 입력은 합성 cluster이며
실제 HarfBuzz 실행을 통과했다고 해석하지 않는다.

`text_layout_native_tests.cpp`는 실제 글꼴·glyph 그리기, 공용 측정의 폭, 입력/문서 질의,
cache 재사용·revision·상한과 발행된 글꼴 참조를 확인하는 Windows 실행 시험이다.
Linux에서는 고정 Skia 헤더로 옵션 OFF/ON·fallback의 구문 컴파일과 플랫폼 독립 시험을 수행한다.
실제 HarfBuzz/ICU 실행, native raster·TSF/Android IME 후보 창, DPI 변경과 20MB 문서의
실제 배치·수정·스크롤 시간은 지원 플랫폼에서 확인해야 한다. 이번 원격 환경의 결과는 진행 기록에 적는다.
