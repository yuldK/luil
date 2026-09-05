# OS 파일 drag & drop

Windows 파일 drop은 내부 drag와 같은 `drag_payload`와 element `drop_target` 모델로 들어온다. platform 계층은 OLE 협상과 좌표를 맡고, UI 모델은 payload를 받을 element와 drop이 만들 액션을 정한다.

element API는 [`include/luil/ui/ui_element.h`](../include/luil/ui/ui_element.h), Windows 연결은 [`src/win32/win32_drop.h`](../src/win32/win32_drop.h)와 `src/win32/window_surface.cpp`에 있다.

## 앱 설정

`window_config::accept_file_drop`을 켜면 주 창과 보조 top-level 창이 OLE 파일 drop을 등록한다. Popup은 잠깐 뜨는 menu 성격의 표면이라 drop target을 등록하지 않는다.

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

OS payload는 `custom_visual == true`이고 셸이 준 경로를 `files`에 순서대로 싣는다. 내부 drag 정체성 필드는 비어 있다. `custom_visual`은 OS가 drag 이미지를 그린다는 뜻이라 library ghost는 겹쳐 그리지 않는다. 보통의 drop 대상 강조는 그대로 남는다.

경로는 UTF-8로 바꾸며 셸 순서를 보존한다. library는 경로를 canonicalize하거나 절대 경로로 바꾼다고 보장하지 않는다.

## 협상과 안전 제한

Win32 adapter는 `IDropTarget`을 구현하고 `CF_HDROP`만 받는다. source가 copy를 허용하고 경로가 하나 이상일 때만 `DROPEFFECT_COPY`를 답한다. Move나 link를 답하지 않는다. 경로만 읽은 뒤 move를 주장하면 source가 앱이 옮기지 않은 원본을 지울 수 있다.

경로 목록은 4,096개로 제한한다. 밖에서 온 data object가 무한한 메모리 작업을 정하지 못하게 그 뒤의 항목은 무시한다.

OLE의 `DragEnter`, `DragOver`, `DragLeave`, `Drop`은 UI STA에서 동기로 온다. 화면 좌표는 대상 표면의 물리 client 좌표로 한 번 바꾼다.

## hover와 drop 흐름

진입, 이동, 떠남은 raw input event로 게시한다. Input controller는 이를 사용해 현재 drag visual과 포인터 아래 수락 element를 interaction snapshot에 게시한다. 따라서 hover 강조는 내부 drag와 같은 경로를 쓴다.

최종 `Drop`은 OLE effect를 동기로 답해야 한다. UI thread가 현재 표면 tree의 `ui_tree::find_drop_target(x, y, payload)`를 직접 묻는다. 이 탐색은 다음 규칙을 따른다.

- 보이는 최상위 수락 element를 찾는다.
- drop target이 아닌 element는 아래 대상을 막지 않는다.
- clip과 visibility를 따른다.
- 완전한 payload로 `accepts`를 부른다.

대상을 찾으면 `on_drop`을 element id와 물리 client 좌표로 부른다. 결과 `input_action`은 보통의 action dispatch를 거치므로 app message, UI command, clipboard action의 thread 규칙이 유지된다.

`Drop` 뒤에는 hover와 drag 상태를 지우는 떠남 event도 게시한다.

## 창 전체 fallback

완료된 drop을 받는 element가 없으면 platform은 `window_delegate::on_file_dropped`을 경로 순서대로 한 번씩 부른다. `true`를 돌려주면 나머지 경로를 보지 않는다. Element 대상은 좌표가 있는 동작을 제공하고, 이 callback은 간단한 창 전체 파일 열기 경로를 유지한다.

element가 payload를 받았으면 fallback은 부르지 않는다.

## 수명과 실패

`com_sta_scope`가 COM STA와 OLE를 초기화한다. 대상 표면은 attach할 때 `IDropTarget` 하나를 등록하고 `HWND`가 사라지기 전에 revoke한다. OLE 초기화나 등록이 실패하면 그 표면의 파일 drop만 꺼지고 나머지 창은 계속 동작한다.

drag event에는 표면 id가 실린다. 보조 창은 자기 tree에서 대상을 강조하고 drop을 해석한다. 출발 표면이 사라지면 그 표면에 매인 gesture 상태를 정리한다.

## 검증

[`tests/win32_drop_tests.cpp`](../tests/win32_drop_tests.cpp)는 경로 순서와 상한, 빈 payload, COM 전달, copy-only 협상, copy를 허락하지 않은 source의 거절, element 액션, fallback을 확인한다. `tests/raster_draw_tests.cpp`는 OS가 drag visual을 소유해도 대상 강조가 남는지 확인한다.
