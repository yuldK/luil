# 가지 단위 팔레트 덮어쓰기

기본 컨트롤을 옅게(잠긴 판·미리 보기) 또는 다른 역할로(위험 구역) 그리려고 앱이 `draw_context`를 필드마다 다시 지어 팔레트만 바꿔 그리면, luil이 문맥에 필드를 더하는 순간 그 앱만 새 필드를 조용히 빠뜨린다. `palette_scope_element`와 `derive_draw_context`가 그 자리를 맡는다.

공개 형식은 [palette_scope_element.h](../include/luil/ui/palette_scope_element.h)와 [ui_element.h](../include/luil/ui/ui_element.h)의 `derive_draw_context`·`draw_context::theme`이다.

## 가지

```cpp
luil::palette_scope_config faded {};
faded.palette = [](luil::ui_color_palette colors, luil::color_theme) {
    colors.primary_foreground = luil::with_alpha(colors.primary_foreground, 0.55f);
    colors.accent = luil::with_alpha(colors.accent, 0.45f);
    return colors;
};
auto scope { std::make_unique<luil::palette_scope_element>(id, std::move(faded)) };
scope->set_content(std::move(panel));
```

- 함수는 바깥 팔레트와 그 테마를 받아 가지의 팔레트를 돌려준다. 그리기마다 불리므로 테마를 바꾸면 다음 frame에 바뀐 테마의 팔레트로 다시 불린다.
- **역할을 고쳐 쓴다.** 컨트롤은 지금처럼 역할(`accent`·`button_hover_background`…)로 그리고, 그 역할의 색만 가지 안에서 다르다. 그래서 hover·눌림·비활성 그림이 그대로 바뀐 색으로 선다.
- 중첩하면 안쪽 함수가 바깥이 바꾼 팔레트를 받는다.
- **고대비에서는 기본으로 바꾸지 않는다**(`in_high_contrast` 거짓). 고대비 팔레트는 사용자가 OS에서 고른 시스템 색이라, 앱이 옅게 하면 대비가 무너진다. 고대비에서도 뜻이 있는 바꿈(위험 구역을 오류색 역할로)만 켠다.
- 키보드 초점 테는 tree가 바깥 팔레트로 마지막에 긋는다. 가지 안에서도 초점은 늘 같은 색으로 보인다.
- 내용은 slot 전체를 받고, 가지는 아무것도 그리지 않는다. 원하는 크기는 내용의 것이다.

## 문맥 짓기

`derive_draw_context(context, palette, scale)`은 팔레트·배율만 바꾼 하위 문맥이다. 문맥을 필드마다 짓는 자리가 라이브러리 안에 이것 하나라, 필드가 늘면 이 함수만 고친다 — 확대 보기와 툴팁 내용도 이것을 쓴다. 돌려준 문맥은 팔레트를 참조로 들므로 팔레트가 문맥보다 오래 살아야 한다.

`draw_context::theme`은 그 팔레트가 난 테마다(플랫폼이 채운다). 고대비를 묻는 그리기가 본다.

## 검증

- `tests/raster_draw_tests.cpp`: 가지 안과 밖의 기본 버튼 채움, 중첩, 고대비의 기본 무시와 `in_high_contrast`, `derive_draw_context`가 나머지 필드를 그대로 옮김.
- 데모 "버튼과 컨트롤" 페이지의 옅게 그린 판.
