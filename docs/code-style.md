# 코드 컨벤션

luil의 C++ 소스는 사람이 규칙에 맞춰 손으로 쓴다. 규칙 전문은 luil과 luil을 쓰는 저장소가 함께 쓰는 공통 규칙(`cpp-code-style`)에 있고, 이 문서는 luil에서 그 규칙이 어떻게 걸리는지만 적는다.

- clang-format으로 저장소나 파일 전체를 정리하지 않는다. formatter는 손으로 고른 줄바꿈을 버리고 다시 채운다. `.clang-format`은 편집기에서 고른 범위에 쓰는 선택 도구다.
- 새로 쓰는 코드와 고치는 줄에 규칙을 적용한다. 형식만을 이유로 기존 코드를 고치지 않는다.
- `third_party/`의 외부 소스는 받은 그대로 둔다.

## 검사

`scripts\check_source_style.ps1`이 검사한다. ctest의 `luil_source_style`과 tooling 대상 `luil_style_check`가 같은 script를 돌린다.

| 보는 것 | 보지 않는 것 (검토로 지킨다) |
| --- | --- |
| UTF-8 무 BOM, CRLF, 줄 끝 공백, 탭 | 중괄호 생략 (단문만) |
| 타입·namespace 이름의 `snake_case` | 뜻 단위 줄 나누기 |
| template 선언과 signature의 줄 분리 | 줄 앞 쉼표, 새 줄의 끝 `;` |
| 여러 줄 블록의 닫는 `}`가 제 줄에 서는지 | 한 줄 함수·enum·`case` |
| 줄 끝 `&&`·`\|\|` | namespace 끝 주석 |
| 줄을 바꾼 뒤의 `{}` | 초기화의 의미, 수명 범위 |
| 형이 글자로 보이는 json 중괄호 초기화 | 함수가 돌려준 json을 `auto x { f() }`로 받기 |

## nlohmann::json 초기화

json 값은 `=`로 받는다. 값 하나를 담은 중괄호는 MSVC에서 복사지만 clang·gcc에서는 initializer_list 생성자가 이겨 원소 하나짜리 배열이 된다 (`{"a":1}`이 `[{"a":1}]`로 온다). Windows에서 통과한 코드가 macOS·iOS·Android에서 조용히 틀린다.

- `json x = y;`·`const auto x = f();`·`return j;`로 쓴다. `json x { y }`·`auto x { j }`·`json m_ { j }`·`return { j };`·`f({ j })`는 쓰지 않는다. 참조(`const json& r { j }`)는 그대로 묶이므로 괜찮다.
- 원소 하나짜리 배열이 뜻이면 `json::array({ v })`로 적는다. 객체 `{ { "k", v } }`와 빈 `{}`는 그대로 쓴다.
- 검사 script는 형이 글자로 보이는 자리만 본다. 나머지는 진단 헤더가 잡는다. `scripts/make_json_brace_trap.ps1`이 만든 json.hpp 사본을 `-I`로 앞세운 빌드에서 시험을 돌리면 그런 자리마다 backtrace를 찍고 멈춘다. clang·gcc 전용이다 (MSVC는 그 자리를 복사로 골라 잡을 것이 없다).

```sh
pwsh -NoProfile -File scripts/make_json_brace_trap.ps1
cmake --preset macos-arm64-core -B build/json-audit -DCMAKE_CXX_FLAGS="-I$PWD/build/json-brace-trap/include"
cmake --build build/json-audit --config Debug
ctest --test-dir build/json-audit -C Debug
```
