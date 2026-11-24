# OS 파일 drag & drop

Windows 파일 drop은 내부 drag와 같은 `drag_payload`와 element `drop_target` 모델로 들어온다. platform 계층은 OLE 협상과 좌표를 맡고, UI 모델은 payload를 받을 element와 drop이 만들 액션을 정한다.

파일 경로만 오는 것이 아니다. 끌기에 실린 데이터 형식의 이름(`drag_payload::formats`)이 함께 오고, 받는 element는 놓는 순간 원시 데이터 객체(`ui_action_context::native_drop_data`, Win32 `IDataObject*`)에서 내용을 꺼낸다. 브라우저가 그림을 끌 때처럼 `CF_HDROP` 없이 가상 파일과 URL만 실은 끌기도 받는다 ([원시 데이터 객체](#원시-데이터-객체)).

element API는 [`include/luil/ui/ui_element.h`](../include/luil/ui/ui_element.h), Windows 연결은 [`src/win32/win32_drop.h`](../src/win32/win32_drop.h)와 [`src/win32/window_surface.cpp`](../src/win32/window_surface.cpp)에 있다.

## 앱 설정

`window_config::accept_file_drop`을 켜면 주 창과 보조 top-level 창, 도킹 표면이 OLE drop을 등록한다. Popup은 잠깐 뜨는 menu 성격의 표면이라 drop target을 등록하지 않는다.

element에는 `drop_target`을 설정한다.

```cpp
luil::drop_target target {};
target.accepts = [](const luil::drag_payload& payload) {
    return payload.files.empty() == false;
};
target.on_drop = [](const luil::drag_payload& payload,
                     const luil::ui_action_context& context) {
    return std::vector<luil::input_action> {
        luil::make_app_action(open_files { payload.files, context.x, context.y })
    };
};
element->set_drop_target(std::move(target));
```

OS payload는 `custom_visual == true`이고 셸이 준 경로를 `files`에 순서대로, 실린 형식 이름을 `formats`에 싣는다. 내부 drag 정체성 필드는 비어 있다. 밖에서 온 끌기인지는 `payload.external()` 하나로 가른다 — 파일 없이 형식만 실린 끌기가 있어 `files`가 비었다고 안에서 시작한 끌기가 아니다. `custom_visual`은 OS가 drag 이미지를 그린다는 뜻이라 library ghost는 겹쳐 그리지 않는다. 보통의 drop 대상 강조는 그대로 남는다.

경로는 UTF-8로 바꾸며 셸 순서를 보존한다. library는 경로를 canonicalize하거나 절대 경로로 바꾼다고 보장하지 않는다.

## 협상과 안전 제한

Win32 adapter는 `IDropTarget`을 구현한다. source가 copy를 허용하고 경로나 형식이 하나 이상일 때만 `DROPEFFECT_COPY`를 답한다. Move나 link를 답하지 않는다. 읽기만 한 뒤 move를 주장하면 source가 앱이 옮기지 않은 원본을 지울 수 있다. 링크만 허락한 source(일부 URL 끌기)는 받지 않는다.

자리마다의 답은 파일 끌기와 형식만 실린 끌기가 다르다.

- **파일 끌기(`CF_HDROP`가 있다)** 는 창 단위로 받는다. 받는 element가 없는 자리도 창 전체 fallback(`on_file_dropped`)이 받으므로 복사를 답한다. 지금까지와 같다.
- **형식만 실린 끌기**는 fallback이 없다. 그 자리의 element가 받을 때만 복사를 답한다. 그렇지 않으면 OS 끌기를 켠 앱이 다른 앱에서 끄는 모든 글·링크에 복사 커서를 보이고, 놓아도 아무 일이 없다.

받기로 한 끌기라도 포인터 자리를 `hit_opaque` 표면(modal scrim·dialog 내용·메뉴·토스트)이 가리고 그 위에 수락 element가 없으면 `DROPEFFECT_NONE`을 답한다. 그 자리에 놓아도 받는 이가 없으므로 커서도 "받지 않음"을 보인다. 끌기 자체는 계속 쥐고 있어 가린 것 밖으로 옮기면 다시 `DROPEFFECT_COPY`가 된다. `Drop`은 아무도 받지 않았을 때만 `DROPEFFECT_NONE`을 답한다.

경로 목록은 1,024개(`max_dropped_paths`), 형식 목록은 256개(`max_drag_formats`)로 제한한다. 밖에서 온 data object가 무한한 메모리 작업을 정하지 못하게 그 뒤의 항목은 무시한다. 형식 열거자는 같은 형식을 매체마다 거듭 답할 수 있어 상한의 네 배까지만 묻고, 같은 이름은 한 번만 싣는다.

OLE의 `DragEnter`, `DragOver`, `DragLeave`, `Drop`은 UI STA에서 동기로 온다. 화면 좌표는 대상 표면의 물리 client 좌표로 한 번 바꾼다.

## hover와 drop 흐름

진입, 이동, 떠남은 raw input event로 게시한다. Input controller는 이를 사용해 현재 drag visual과 포인터 아래 수락 element를 interaction snapshot에 게시한다. 따라서 hover 강조는 내부 drag와 같은 경로를 쓴다.

최종 `Drop`은 OLE effect를 동기로 답해야 한다. UI thread가 현재 표면 tree의 `ui_tree::drop_hit_test(x, y, payload)`를 직접 묻는다. `DragEnter`·`DragOver`의 effect도 같은 탐색으로 정한다. 이 탐색은 다음 규칙을 따른다.

- 보이는 최상위 수락 element를 찾는다.
- drop target이 아닌 보통 element는 아래 대상을 막지 않는다.
- `hit_opaque` element는 막는다. 그 안의 수락 element는 찾지만, 그것이 가린 아래 대상은 찾지 않고 결과를 `blocked`로 알린다. 내부 drag의 대상 탐색(`find_drop_target`)도 같은 규칙이다.
- clip과 visibility를 따른다.
- 완전한 payload로 `accepts`를 부른다. 자리마다 받기를 가르는 대상(`drop_target::accepts_at`)은 그 좌표로도 묻고, 거절한 자리는 받지 않은 것이라 아래 대상으로 내려가 다시 찾는다.

대상을 찾으면 `on_drop`을 element id와 물리 client 좌표로 부른다. 문맥의 수식 키(`control`·`shift`·`alt`)는 OLE가 `Drop`에 실은 키 상태(`MK_CONTROL`·`MK_SHIFT`·`MK_ALT`)다 — 다른 프로그램에서 끌어오는 동안 키보드 입력은 그쪽에 있어서, 이 thread의 `GetKeyState`는 낡은 값이다. 결과 `input_action`은 보통의 action dispatch를 거치므로 app message, UI command, clipboard action의 thread 규칙이 유지된다.

`Drop` 뒤에는 hover와 drag 상태를 지우는 떠남 event도 게시한다.

## 창 전체 fallback

완료된 drop을 받는 element가 없으면 platform은 `window_delegate::on_file_dropped`을 경로 순서대로 한 번씩 부른다. `true`를 돌려주면 나머지 경로를 보지 않는다. Element 대상은 좌표가 있는 동작을 제공하고, 이 callback은 간단한 창 전체 파일 열기 경로를 유지한다.

element가 payload를 받았으면 fallback은 부르지 않는다. 불투명 표면이 가린 자리(`blocked`)에서도 부르지 않는다. modal이 떠 있는 동안 뒤의 창이 파일을 여는 일이 없다.

## 안에서 시작한 끌기와의 관계

안에서 시작한 끌기(탭·목록 행)는 OLE를 쓰지 않는다. 같은 `drag_payload`와 `drop_target`을 쓰지만 끄는 동안의 포인터를 입력 controller가 직접 따라가므로, 앱 밖(탐색기·다른 앱)에 놓을 수는 없고 앱의 창들 사이에서만 놓인다. 두 끌기의 갈림은 다음과 같다.

| | OS 파일 끌기 | 안에서 시작한 끌기 |
| --- | --- | --- |
| 포인터를 쥐는 쪽 | OLE (`DoDragDrop`은 밖의 source가 돈다) | 누른 창의 캡처 |
| 다른 창으로 넘어감 | 창마다 `DragEnter`·`DragLeave`가 온다 | 플랫폼이 화면에서 포인터 밑 창을 찾아 이벤트에 싣는다(`pointer_over`, [여러 top-level window](multi-window-design.md)) |
| 놓을 자리 탐색 | UI thread가 그 표면 tree에 동기로 묻는다 | input thread가 포인터가 놓인 표면 tree에서 찾는다 |
| 받는 대상이 없을 때 | 창 전체 fallback(`window_delegate::on_file_dropped`) | 끌기를 시작한 쪽의 `drag_source::on_unclaimed_drop` |
| 불투명 표면이 가린 자리 | fallback을 부르지 않는다 | `on_unclaimed_drop`을 부르지 않는다 |
| 창 밖의 그림 | 셸이 그린다 (`custom_visual`) | 플랫폼의 투명 창이 그린다 |

받는 대상이 없는 놓기를 알리는 규칙이 한 벌이다. 불투명 표면이 가린 자리는 빈 자리가 아니라 막힌 자리라, 파일 끌기의 fallback도 안에서 시작한 끌기의 알림도 그 자리에서는 서지 않는다 — modal이 떠 있는 동안 뒤의 창이 파일을 열지 않는 것과 탭이 떼어지지 않는 것이 같은 이유다.

받는 쪽이 자기 강조를 그리는 대상(`drop_target::custom_highlight` — 탭 막대의 틈 표시)은 기본 테두리를 그리지 않는다. 파일 끌기에도 같다.

## 원시 데이터 객체

브라우저는 그림을 끌 때 `CF_HDROP` 없이 가상 파일(`FileGroupDescriptorW` + `FileContents`)과 URL·HTML 조각만 준다. 메일·압축 프로그램도 디스크에 없는 파일을 가상 파일로 준다. 경로 목록만으로는 이런 끌기를 받을 수 없어, 형식 목록과 원시 객체를 함께 넘긴다.

```cpp
luil::drop_target target {};
// 들어온 순간에는 형식만 보고 받을지 답한다.
target.accepts = [](const luil::drag_payload& payload) {
    return payload.has_format(u8"FileGroupDescriptorW") || payload.files.empty() == false;
};
// 놓는 순간 그 자리에서 내용을 꺼낸다. 꺼낸 값을 메시지에 담는다.
target.on_drop = [](const luil::drag_payload& payload, const luil::ui_action_context& context) {
    images_dropped intent {};
    for (const luil::win32::dropped_virtual_file& file : luil::win32::drop_virtual_files(context.native_drop_data))
        intent.names.push_back(file.name);
    if (auto bytes { luil::win32::drop_data_bytes(context.native_drop_data, u8"FileContents", 0) }; bytes.has_value())
        intent.first = std::move(*bytes);
    return std::vector<luil::input_action> { luil::make_app_action(std::move(intent)) };
};
```

- **기존 앱이 볼 것.** 지금까지 OS 끌기는 늘 경로를 실었으므로 `payload.files.empty()`를 "안에서 시작한 끌기"로 읽은 판정이 있을 수 있다. 이제 형식만 실린 끌기는 `files`가 비어도 밖에서 온 것이라, 그런 판정은 `payload.external() == false`로 바꾼다. "자기 목록에서 온 것이 아니면 받는다"처럼 출발을 보지 않는 판정은 브라우저의 글·링크 끌기도 받게 된다. 라이브러리의 목록·표·탭·도킹은 출발 종류와 키를 보므로 형식만 실린 끌기를 받지 않는다.
- **형식 이름.** 등록 형식은 OS가 준 이름 그대로다 (`FileGroupDescriptorW`, `FileContents`, `UniformResourceLocatorW`, `HTML Format`). 이름이 없는 표준 형식은 상수 이름이다 (`CF_HDROP`, `CF_UNICODETEXT`, `CF_DIB` …). 순서는 data object가 알린 순서다. 파일 끌기면 `CF_HDROP`도 든다.
- **들어올 때는 형식만 본다.** `DragEnter`에서 형식을 한 번 열거하고, 내용은 읽지 않는다. `accepts`는 input thread(강조)와 UI thread(답) 양쪽에서 불리므로 형식만 보는 가벼운 판정이어야 한다.
- **놓을 때 꺼낸다.** OLE는 내용을 `Drop` 안에서 꺼내라고 한다. 그래서 원시 객체는 payload에 싣지 않고, UI thread가 `Drop` 안에서 부르는 `on_drop`의 문맥(`native_drop_data`)으로만 넘긴다. payload는 값으로 input thread에 복사되고 발행본에 남으며 `operator==`가 있어, COM 포인터를 담으면 thread와 수명을 넘는다.
  - 포인터는 그 `on_drop` 호출 동안만 유효하다. 쥐어 두거나 다른 thread로 넘기지 않고, 참조 수도 넘기지 않는다.
  - 내용은 `Drop`이 준 객체에서 꺼낸다. 들어올 때의 것과 대개 같지만 OLE가 그 호출에 준 것이 계약이다.
  - 안에서 시작한 끌기와 Android에서는 nullptr다.
- **도우미.** [`luil/win32/drop_data.h`](../include/luil/win32/drop_data.h)가 흔한 셋을 덮는다. 원시 객체를 `IDataObject*`로 바꿔 직접 써도 된다.
  - `drop_virtual_files`: `FileGroupDescriptorW`의 이름·크기·폴더 여부다. 색인이 `FileContents`의 `lindex`다. 블록이 알린 개수보다 짧으면 들어 있는 만큼만 읽는다.
  - `drop_data_bytes`: 그 형식의 내용을 `HGLOBAL`·`IStream` 매체에서 읽는다. 상한(기본 64 MiB)을 넘으면 잘린 내용 대신 빈 값이다. 흐름은 앞으로 되감아 읽는다.
  - `drop_data_text`: UTF-16 글 형식(`CF_UNICODETEXT`, `UniformResourceLocatorW`)을 첫 NUL까지 UTF-8로 읽는다.
- **커서 효과.** 복사·없음 둘뿐이고 source가 허락한 집합 안에서만 고른다. 앱이 그 내용으로 무엇을 하든 원본은 source가 지우지 않는다.
- **활성화하지 않는다.** 끄는 동안 어느 표면도 앞 창을 빼앗지 않는다. OLE 끌기는 그 자체로 대상 창을 활성화하지 않고, luil도 진입·이동·놓기에서 창을 앞으로 세우지 않는다. 드래그 중에 앞 창을 빼앗으면 원본 앱의 끌기가 깨질 수 있다. 도킹 표면(docked-surface-design.md)은 눌러서도 활성화되지 않는다.

## 끄는 그림

셸이 그리는 끄는 그림(`IDragSourceHelper`)은 대상 창이 `IDropTargetHelper`에 알려야 그 창 위에서도 보인다. 알리지 않으면 탐색기·브라우저가 끌고 온 그림이 우리 창 위에서만 사라진다. 표면마다의 `IDropTarget`이 `DragEnter`·`DragOver`·`DragLeave`·`Drop`을 그대로 넘긴다.

- 받지 않는 끌기에도 넘긴다. 커서가 "받지 않음"을 보이는 동안에도 무엇을 끄는지는 보인다.
- 넘기는 효과는 그 자리에서 답한 것이다. 셸이 그림에 붙이는 표식(복사·금지)이 커서와 같다.
- 만들지 못하면(`CLSID_DragDropHelper`) 그림만 빠지고 끌기는 그대로다.
- 내부 drag ghost와는 겹치지 않는다. OS payload는 `custom_visual`이라 library ghost를 그리지 않는다.

## 수명과 실패

`com_sta_scope`가 COM STA와 OLE를 초기화한다. 대상 표면은 attach할 때 `IDropTarget` 하나를 등록하고 `HWND`가 사라지기 전에 revoke한다. OLE 초기화나 등록이 실패하면 그 표면의 파일 drop만 꺼지고 나머지 창은 계속 동작한다.

drag event에는 표면 id가 실린다. 보조 창은 자기 tree에서 대상을 강조하고 drop을 해석한다. 출발 표면이 사라지면 그 표면에 매인 gesture 상태를 정리한다. 안에서 시작한 끌기도 출발 표면이 사라지면 놓기 없이 취소된다 — 그 창이 쥔 캡처가 함께 풀려 뗌을 받을 수 없기 때문이다.

## 검증

[`tests/win32_drop_tests.cpp`](../tests/win32_drop_tests.cpp)는 경로 순서와 상한, 빈 payload, COM 전달, copy-only 협상, copy를 허락하지 않은 source의 거절, 가린 자리의 NONE 답, element 액션, fallback과 불투명 표면이 막은 fallback을 확인한다. 형식 이름과 표준 상수의 왕복, 가상 파일만 실은 source의 형식 목록(같은 형식 한 번), host로 가는 형식과 `Drop`의 객체, 링크만 허락한 source의 거절, 형식만 실린 끌기가 받는 element 위에서만 받히는 것과 그 `on_drop`이 원시 객체로 가상 파일 이름·`FileContents`(되감은 흐름)·URL을 꺼내는 것, 도우미의 상한과 없는 형식도 같은 파일이다. 실제 브라우저 끌기와 끄는 그림은 사람 손으로 확인한다 (데모 창 페이지의 도킹 표면). [`tests/raster_draw_tests.cpp`](../tests/raster_draw_tests.cpp)는 OS가 drag visual을 소유해도 대상 강조가 남는지 확인한다. 자리마다 받기를 가르는 대상이 거절한 자리에서 아래 대상이 답하는 것과, 안에서 시작한 끌기의 받는 대상 없는 놓기·취소·불투명 표면 규칙은 [`tests/ui_interaction_tests.cpp`](../tests/ui_interaction_tests.cpp)가 확인한다.

## macOS

`application_config::accept_file_drop`을 켜면 주 창·보조 창·도킹 표면의 Skia view가 끌기를 받는다 ([`macos_app.mm`](../src/macos/macos_app.mm)). 받는 형식은 파일 URL, 주소, 글, PNG, TIFF다.

- 들어옴·움직임·떠남은 Windows처럼 표시만 세우고(`file_drag_*_event`), 답과 놓기는 그 창의 tree에 직접 묻는다. popup 위는 받지 않는다 (Windows의 popup은 drop을 등록하지 않는다).
- 효과는 늘 복사다. 놓는 순간의 수식 키는 Command·Control이 `control`, Option이 `alt`다.
- 원시 데이터(`native_drop_data`)는 끌기의 `NSPasteboard*`다. [`drop_data.h`](../include/luil/win32/drop_data.h)의 도우미가 붙여넣기 판의 형식(UTI)을 읽고, 흔한 Win32 이름(`CF_UNICODETEXT`, `UniformResourceLocatorW`, `HTML Format`, `PNG`)을 같은 뜻의 형식으로 옮긴다. 가상 파일은 파일 약속(`NSFilePromiseReceiver`)의 이름만 읽고 내용은 바이트로 오지 않는다.
- 받는 element가 없으면 delegate가 `win32::window_delegate`를 구현했을 때 `on_file_dropped`로 물러선다.
