# 나중에 서는 초점과 창 활성화

앱이 "보조 창을 띄우고 그 안의 입력 칸에 초점", "목록을 그 행까지 흘린 뒤 그 행에 초점"을 원할 때, 그 대상은 요청하는 frame에 아직 없다. 창은 UI thread가 다음 대조에서 만들고, 행은 흘린 뒤 다시 지은 tree에 처음 선다. `ui_frame::focus`는 그 요청을 luil이 들고 있다가 대상이 서는 순간 한 번 이행하는 길이다.

공개 형식은 [app_host.h](../include/luil/app/app_host.h)의 `focus_request`·`focus_request_outcome`·`ui_frame::focus`·`ui_frame::focus_report`다.

## 왜 앱이 기다리지 않는가

앱이 타이머로 tree를 다시 보며 대상이 서기를 기다리면, 앱은 대상이 **UI thread가 본 tree**에 섰는지를 모른다. logic thread가 지은 tree가 게시되고, UI thread가 그것을 집어 창을 만들고 배치한 뒤에야 초점을 세울 수 있다. 그 순서를 아는 것은 UI thread뿐이다. 그래서 기다림은 UI thread가 frame을 대조하는 자리에서 한다.

초점은 tree가 아니라 입력 상태라 UI thread가 직접 세우지 않는다. 이행은 보조 기술의 초점 요청과 같은 `access_focus_event`를 입력 thread로 보내는 것이다(accessibility-action-design.md). 그래서 키보드로 옮긴 초점과 같이 테가 그려지고, 초점 칸의 드러냄 규칙도 같다.

## 요청

```cpp
frame->focus = luil::focus_request {
    .id = ++focus_revision_,
    .surface = u8"tool-window",
    .target = luil::ui_element_id { kind_input, u8"tool/search" },
    .activate = true,
};
frame->focus_report = [](std::uint64_t id, luil::focus_request_outcome outcome) {
    return luil::app_message { focus_done_intent { id, outcome } };
};
```

- **번호가 바뀔 때만 새 요청이다.** `window_placement_revision`과 같은 규칙이다. 앱은 같은 요청을 frame마다 실어도 되고, 한 번 이행한 번호는 다시 이행하지 않는다. 0은 요청이 없다는 뜻이다.
- `surface`는 표면 id다. 비어 있으면 주 창, 아니면 `ui_window::id`나 `ui_popup::id`다.
- `target`이 비어 있으면 초점은 그대로 두고 표면만 활성화한다(`activate`).
- `activate`는 그 표면의 창을 앞으로 세우고 활성화한다. 최소화돼 있으면 되살린다. popup은 활성화를 받지 않는 표면이라 무시한다.
- `timeout`(기본 2초) 안에 대상이 서지 않으면 버린다.

## 대상이 섰다는 것

`focus_target_ready(tree, target)`가 참일 때다.

- 그 표면의 게시된 tree에 대상이 있고,
- 초점을 받을 수 있으며(`focusable` — 활성, 보임, 배치됨, Tab의 자리이거나 텍스트 칸),
- 보이는 사각형(`ui_tree::visible_bounds`)이 표면 안에 조금이라도 걸친다.

흘려 감춘 칸, 접힌 묶음 안의 칸은 아직 서지 않은 것이다. 앱이 같은 frame에서 그 칸을 드러내는 스크롤을 함께 실으면, 다시 지은 tree에서 칸이 창 안에 서는 순간 이행된다.

가상 목록의 행은 Tab의 자리가 아니다 — 자리는 목록 자신이고 행은 커서다(virtual-list-design.md). "그 행에 초점"은 앱이 커서를 그 행으로 옮기고 드러내는 스크롤을 실은 뒤 **목록**에 초점을 청하는 것이다. 목록은 이미 서 있으므로 곧바로 이행되고, 커서 행이 드러나는 것은 목록의 규칙이다.

## 결과 알리기

`focus_report`가 있으면 요청 하나마다 결과를 한 번 알린다.

| 결과 | 때 |
| --- | --- |
| `fulfilled` | 대상이 서서 초점 요청(과 활성화)을 보냈다 |
| `expired` | 기한 안에 대상이 서지 않았다 |
| `superseded` | 이행 전에 새 번호의 요청이 왔다 |

- 덮어쓰기는 마지막 요청이 이긴다. 들고 있던 요청은 `superseded`로 알리고 버린다.
- 표면이 frame에서 빠져도 요청은 남는다. 같은 id의 창이 기한 안에 다시 서면 이행되고, 아니면 `expired`다.
- 결과는 앱 메시지라 logic thread로 간다.

## UI thread에서 하는 일

- frame 대조(`snapshot_wake`)가 끝난 뒤 새 요청을 받고, 들고 있는 요청이 이행될 수 있는지 본다. 창과 popup을 먼저 맞춰야 새 보조 창의 tree를 볼 수 있다.
- 기한을 지키려고 요청을 들고 있는 동안 기한 타이머를 건다. frame이 오지 않아도 기한에 `expired`를 알린다.
- 활성화는 Win32에서 `SetForegroundWindow`다. 앱이 앞 창이 아닐 때는 OS가 막고 작업 표시줄 단추만 깜빡인다 — 앞 창을 빼앗지 않는 OS 규칙이라 luil이 넘지 않는다. 그래도 초점 요청은 보낸다. 창이 활성화되면 그 초점이 이미 서 있다.
- 가짜 마우스 누름으로 초점을 세우지 않는다. 입력 칸의 초점은 논리 초점이고, 창이 키를 받는 것은 창 활성화의 일이다.

## Android

창이 하나라 `surface`는 주 표면뿐이다. 초점 요청은 같은 길로 이행한다. `activate`는 지원하지 않는다 — Activity를 앞으로 세우는 것은 앱의 Java 쪽 일이고 luil은 Java 코드를 싣지 않는다. 활성화만의 요청(`target`이 빈 것)은 곧바로 `fulfilled`다.

## 검증

- `tests/focus_request_tests.cpp`: 대상이 다음 frame에 생길 때 이행, 같은 번호의 재게시를 다시 이행하지 않음, 덮어쓰기의 `superseded`, 기한의 `expired`, 보이지 않는 대상(숨김·표면 밖·비활성)은 서지 않음, 흘리는 창 밖의 칸은 흘러 들어온 뒤에 섬.
- 데모 "여러 창" 페이지의 "도구 창 열고 검색 칸에 초점" 단추가 새 보조 창의 입력 칸에 초점을 세운다.
