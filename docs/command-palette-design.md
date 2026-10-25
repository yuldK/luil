# 명령 팔레트

앱마다 명령 팔레트를 직접 짓고 있었다. 검색 칸과 메뉴를 쌓고, 질의로 명령을 거르고, "일치하는 항목 없음" 줄을 넣고, 단축키 열을 채우고, 실행한 뒤 닫았다. 그 가운데 Enter로 첫 결과를 실행하는 것은 메뉴가 길을 주지 않아 아무도 하지 못했다. 이제 라이브러리가 `app_command` 목록 하나로 팔레트를 짓는다.

공개 형식은 [command_palette.h](../include/luil/ui/command_palette.h)와 [menu_element.h](../include/luil/ui/menu_element.h)의 `menu_config::default_key`·`after_select`다.

## 짓기

```cpp
luil::command_palette_config palette {};
palette.owner = u8"commands";
palette.commands = commands_;          // 단축키 판정에 쓰는 그 명령표다
palette.query = make_text_input_view(query_, composition_, target_palette);
palette.placeholder = u8"명령 찾기";
palette.empty_label = u8"일치하는 명령 없음";
palette.run = [](const std::u8string& id) { return luil::make_app_action(run_command_intent { id }); };
palette.close = luil::make_message_action(close_palette_intent {});
popup.height = luil::command_palette_height(palette);
popup.tree = luil::make_arranged_tree(luil::make_command_palette(std::move(palette)), { 0, 0, width, popup.height }, scale);
```

- **명령표는 앱이 이미 든 것이다.** 단축키 판정(`match_command`)과 메뉴(`menu_item_from`)가 쓰는 그 `app_command` 목록을 그대로 넘긴다. 줄은 `menu_item_from`으로 짓는다. 그래서 단축키 열은 사용자가 다시 정한 단축키까지 따른 실제 단축키이고, 꺼진 명령은 흐리게 서며 고를 수 없고, 체크 명령은 체크로 선다.
- 질의 글은 앱의 편집 초안이다. 칸의 kind는 `ui_element_kind::command_palette_search`라, 앱 정책의 `text_target_of`가 그 kind에 대상을 준다(검색 메뉴와 같은 규칙).
- 팔레트는 메뉴다. 앱 정책의 `menu()`가 팔레트가 열린 동안 `{ menu, menu_item }`을 답하면 ↑↓가 결과를 돌고, Enter가 실행하며, Esc는 `close_menu`로 간다. 다른 키는 검색 칸으로 간다(검색 메뉴와 같다).

## 거르기

`filter_commands(commands, query)`가 맞는 명령의 번호를 순서대로 준다. 팔레트도 이 함수로 거른다.

- 대소문자는 `fold_case`로 접는다. "SAVE"로 "저장(Save)"을 찾는다.
- 차례는 세 단계다. 이름이 질의로 시작하는 것, 이름 안의 낱말이 질의로 시작하는 것, 이름 어딘가에 질의가 든 것 순이다. 같은 단계 안에서는 명령표의 차례를 지킨다. 명령 id에 질의가 든 것은 넷째 단계다. 그래서 앱이 붙인 영문 id("file.save")로도 한글 이름의 명령을 찾는다.
- 빈 질의(앞뒤 공백만 있는 것 포함)는 전부를 명령표 차례로 준다.
- IME 조합 중에는 `search_query`의 규칙을 따른다. 조합 글을 넣은 질의가 아무것도 맞히지 못하면 확정 글로 물러선다. 그래서 "저ㅈ"을 치는 동안에도 목록이 비었다 살아나기를 되풀이하지 않는다.

## Enter·실행·닫기

- **Enter는 강조된 결과를, 강조가 없으면 첫 결과를 실행한다.** 팔레트는 메뉴의 `default_key`에 첫 결과(고를 수 있는 것 가운데 첫째)를 둔다. 강조가 없거나, 강조한 항목이 질의가 바뀌어 사라졌으면 Enter가 그 항목을 고른다. 기본 항목은 강조가 없을 때 강조와 같은 바탕으로 그려진다. 그래서 무엇이 실행될지 보인다.
- **실행하면 닫힌다.** 팔레트는 메뉴의 `after_select`에 `close`를 둔다. 항목을 고르면 `run(id)` 다음에 `close`가 함께 나간다. 앱은 실행 메시지를 받아 명령을 돌리고, 닫기 메시지로 팔레트 상태를 지운다. 둘을 한 메시지로 묶지 않은 것은 앱이 메뉴·단축키·팔레트에서 같은 실행 메시지를 쓰게 하려는 것이다.
- 결과가 없으면 `empty_label`의 흐린 줄 하나다. 고를 수 없고, Enter도 아무것도 하지 않는다.

## 메뉴에 더한 두 손잡이

둘 다 팔레트 밖의 메뉴에도 쓴다.

- `default_key`: 강조가 없을 때 Enter가 고를 항목이다. 비어 있으면 지금처럼 Enter가 아무것도 하지 않는다.
- `after_select`: 끝 항목(하위 메뉴가 없는 것)을 고르면 `select` 다음에 함께 내는 메시지다. "고르면 닫히는 메뉴"를 앱 쪽 두 메시지 처리 없이 짓는다.

## 진입 단추

`make_command_palette_entry(config)`는 제목 표시줄 가운데에 두는 검색 모양 단추다.

- 돋보기 글리프와 글("명령 찾기…")을 단다. 툴팁 끝에 단축키를 붙인다(`label_with_shortcut`).
- `set_caption_client(true)`라 끌기 띠 위에서도 눌린다(caption-button-design.md).
- 누르면 `open`이 나간다. 같은 메시지를 단축키(흔히 Ctrl+Shift+P)에도 건다.

## Android

플랫폼 쪽은 나누지 않는다. 팔레트는 공용 element(검색 칸, 메뉴)라 터치에서도 같다. 소프트 키보드의 확정 단추는 검색 칸의 `submit`이 아니라 메뉴의 Enter 경로를 탄다.

## 검증

- `tests/command_palette_tests.cpp`:
  - 거르기의 차례(시작·낱말 시작·포함·id), 대소문자, 빈 질의, 조합 중 물러섬;
  - 줄의 단축키 열(다시 정한 단축키), 꺼진 명령, 결과 없음 줄;
  - 첫 결과가 Enter의 기본 항목인지, 강조가 사라졌을 때 Enter가 기본 항목을 실행하는지;
  - 실행 뒤 닫기 메시지, 진입 단추의 caption client 표시와 툴팁.
- `tests/menu_element_tests.cpp`: `after_select`가 끝 항목에만 붙는지. `default_key`는 위의 Enter test가 controller로 본다.
- 데모: 제목 표시줄의 "명령 찾기…" 단추와 Ctrl+Shift+P가 데모 명령 팔레트(페이지 이동·사이드바 접기)를 연다. 열면 검색 칸에 초점이 선다. 사이드바 접기의 Ctrl+B는 팔레트에 보이는 같은 명령표를 `match_command`로 맞춘다.
