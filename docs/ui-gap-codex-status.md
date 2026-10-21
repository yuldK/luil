# Codex 트랙 진행·검증

기준 커밋은 `dcd29088ee44411cb4d82bfe6aa006888c28a44e`, 작업 브랜치는
`codex/ui-gap-codex-track`이었다. X8은 `codex/x8-text-layout`에서 이어서 구현했다. 다른 사용자 프로젝트는 조사하지 않았다.
Claude의 C0는 현재 브랜치에 없으며 그 트랙의 키 조상 전파·휠·역할 enum을 구현하지 않는다.

| 단위 | 구현 | 확인한 범위 | 남은 검증 |
| --- | --- | --- | --- |
| X0 | 가로 막대·라벨 정렬/가운데 말줄임·스피너·가운데 버튼 | 변경 소스·시나리오 테스트의 구문 컴파일 | raster, Windows 입력·UIA 실행 |
| X1 | Unicode 16 글자소 경계·Backspace 정책·편집/클릭/말줄임 연결 | 내장 경계와 기존 편집 테스트 실행, SkUnicode 구성 구문 컴파일 | 플랫폼 ICU/libgrapheme 동일 경계 시험 실행 |
| X2 | 읽기 전용·줄 넘김 해제·가로 스크롤·코드 글꼴·글자 크기 | UI 소스 구문 컴파일, 읽기 전용 Android IME 세션 테스트 실행 | Windows TSF/UIA·Android 기기·raster 실행 |
| X3 | 체크/라디오 메뉴·단축키 열·폭 질의 | UI 소스·테스트 구문 컴파일 | Narrator, 메뉴 시나리오·raster 실행 |
| X4 | 팝업 사슬·하위 메뉴·명령표 | 팝업 대조·순환 거절·명령표 실행, 메뉴 시나리오 구문 컴파일 | Win32/Android 중첩 창·IME·실제 hover 실행 |
| X5 | 스타일 구간·장식·고정 여백·검색 강조·배치 질의 | 20MB/50만 구간의 가시 범위 질의 실행, UI 소스 구문 컴파일 | 실제 배치/스크롤 계측·고대비/raster 실행 |
| X6 | 비동기 후보 token·필터·popup·키 handler·검증 자리 | 오래된 응답/중복 응답 테스트 실행, UI 소스·키 시나리오 구문 컴파일 | **C0 병합 후 키 전파 연결**, IME·popup 클릭 UI 실행 |
| X7 | 파일/폴더 요청·선택/취소/실패·중복 억제·Win32/Android host·데모 전환 | 중복/재진입 gate와 UTF-8 결과 테스트 실행, 공개 API·이미지 페이지 구문 컴파일 | Win32 대화상자·복수 선택·소유 창, Android unsupported 전달 실행 |
| X8 | 승인된 [공통 텍스트 레이아웃](text-layout-design.md), HarfBuzz/primitive·glyph cache·입력/문서 연결 | OFF/ON/fallback 구문 검사·플랫폼 독립 기하 실행 시험 | native HarfBuzz/raster·IME·DPI·20MB 배치 계측, 후속 UAX #14·양방향 편집 |

## X0–X7 검증 결과

사용자 요청에 따라 개발 중에는 구문 컴파일을 우선하고, 마지막에 실행 가능한 전체 묶음을 한 번 실행했다.
전체 앱 빌드를 실행한 것은 아니다. Linux에서 저장소의 Windows/Android CMake 진입점은 지원되지 않는다.

- `g++ -std=c++20 -fsyntax-only -Wall -Wextra`: 변경 플랫폼 독립 UI/텍스트 소스·테스트·공개 umbrella와
  SkUnicode 구성을 포함한 **35개 검사 통과**. Android IME 세션과 새 읽기 전용 테스트도 컴파일·링크했다.
- 실제 고정 Skia Release 패키지의 SHA-256을 확인한 헤더를 사용했다. 이미지/OS 대역으로 UI 동작을 통과 처리하지 않았다.
- PowerShell 7.5.4의 `scripts/check_source_style.ps1`: **608개 파일 통과**.
- 외부 portable harness의 마지막 CTest: **160개 통과, 실패/건너뜀 0개**, 1.02초.
  플랫폼 독립 135개, messaging ASan 19개, CMake 6개다. 기존 편집·네트워크·messaging 회귀와
  새 글자소·팝업·명령표·문서 질의·후보 상태·대화상자 gate·읽기 전용 IME 시험이 포함된다.
- CMake 6개는 Codicons checksum, umbrella, core portability, accent/style 생성과
  Unicode 고지의 소스/설치 resource 위치 생성 결과 비교다. 마지막 것은 외부 검증 helper다.
- 20MB·50만 구간에서 끝의 가시 50개를 복사 없이 참조하는지 확인했다.
  UI 프레임 시간이나 실제 문서 배치 성능 수치로 해석하지 않는다.

실행 기록은 `/workspace/.luil-environment/ctest-codex.log`, JUnit은
`/workspace/.luil-environment/ctest-codex-results.xml`, 구문 검사별 기록은
`/workspace/.luil-environment/syntax-results.json`이다.

Win32/Android 소스의 실제 플랫폼 컴파일과 실행, Skia raster/UI tests,
Debug/Release 전체 테스트와 설치 소비자 빌드는 미실행이다. 해당 도구체인과 실행 환경이 필요하다.
C0가 없으므로 X6 전체가 준비되었다고 보고하지 않는다.

## X8 검증 결과

사용자의 설계 승인 뒤 불변 `text_layout`·기존 공장 기반 native shaping·primitive 구현,
클러스터/합자 caret·선택·줄 넘김, 라벨/버튼·한 줄 입력·문서 보기 연결을 구현했다.
공용 glyph cache는 16MiB·512항목 LRU이고, 문단 cache와 가시 줄 값은 별도로 유지한다.
옵션 OFF와 사용자 측정기를 주입한 기존 컨트롤 경로는 유지한다.

- 구문 컴파일 **37개 검사 통과**: 기본 OFF, ON의 HarfBuzz/ICU 정의, fallback 정의,
  변경 UI·폰트/텍스트 소스·새 기하/native 테스트·관련 기존 테스트와 공개 umbrella를 포함한다.
- PowerShell 소스 스타일 **615개 파일 통과**.
- 마지막 실행 가능한 CTest 묶음 **167개 통과, 실패/건너뜀 0개**, 0.97초.
  플랫폼 독립 142개, 메시징 ASan 19개, CMake 6개다. 실행 가능한 전체 묶음은 마지막에 한 번만 실행했다.
- 새 7개 실행 시험은 원문 소유권·글자소 왕복, wrap affinity·선택, CRLF/빈 줄,
  합성 합자 cluster·줄 경계 재배치, 동일 x/중간점 caret, 동시 읽기와 일괄 경계 일치를 확인했다.
  합성 cluster 시험은 실제 HarfBuzz 실행 검증이 아니다.
- 실제 글꼴·glyph raster, 측정 폭, 입력/문서 좌표, cache 상한/revision과 글꼴 snapshot은
  Windows `text_layout_native_tests.cpp`에 추가하고 구문 컴파일했다. 여기서는 실행하지 않았다.

실행 기록은 `/workspace/.luil-environment/ctest-x8.log`, JUnit은
`/workspace/.luil-environment/ctest-x8-results.xml`, 구문 검사별 기록은
`/workspace/.luil-environment/syntax-x8-results.json`이다.
Win32/Android 전체 빌드와 실제 HarfBuzz/ICU·raster·IME·DPI 변경,
20MB 문서의 실제 초기 배치·수정·스크롤 시간은 이 환경에서 미검증이다.
UAX #14와 양방향 시각 편집은 승인된 설계대로 후속 단계다.

## 리뷰 단위와 게시

X0–X7은 각각 별도 기능 커밋과 `codex/x0-shared-controls`부터 `codex/x7-file-dialog`까지
로컬 리뷰 참조를 두었다. 이 참조는 의존 순서대로 쌓인 중간 시점이며 마지막 보완은 작업 브랜치에 있다.
의존 단위를 먼저 병합한 뒤 각 커밋 차이로 리뷰한다. 승인 후 X8 구현은 `codex/x8-text-layout`에 별도 커밋으로 둔다.

GitHub API 읽기(`gh api repos/yuldK/luil`)는 프록시에서 Forbidden이었다.
작업 브랜치의 `git push --set-upstream origin codex/ui-gap-codex-track`도 저장소 쓰기 권한 403으로 거절되었다.
원격 브랜치·PR은 생성하지 못했으며 로컬 브랜치·커밋을 원격 게시 완료로 표시하지 않는다.
공유 파일을 고친 이유는 [인계 계약](ui-gap-codex-track.md#구현-중-추가-계약)에 기록했다.
