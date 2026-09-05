# Tree 배치 진입점과 진단

`ui_tree`는 배치가 끝난 element 계층을 소유한다. Root가 자손 전체를 배치하는 일반 tree는 `make_arranged_tree()`로 만들고, 부모 element는 `arrange_context::for_child()`로 배치 문맥을 전달한다.

관련 API는 [ui_element.h](../include/luil/ui/ui_element.h)와 [ui_tree.h](../include/luil/ui/ui_tree.h)에 있다.

## Arrange 문맥

`arrange_context`는 세 값을 담는다.

```cpp
struct arrange_context {
    rect_f slot;
    float scale;
    float scroll_offset;
};
```

`slot`은 물리 픽셀 좌표와 크기다. `scale`은 논리 길이를 물리 픽셀로 바꾸는 배율이다. `scroll_offset`은 세로 스크롤과 sticky 배치가 공유하는 논리 픽셀 값이다.

부모가 자식 slot만 바꿀 때는 `context.for_child(child_slot)`을 사용한다.

```cpp
child->arrange(context.for_child({ x, y, width, height }));
```

이 함수는 scale과 scroll offset을 그대로 이어 준다. 문맥에 필드가 추가되어도 모든 컨테이너가 새 값을 자동으로 전달할 수 있다.

세로 `scroll_view_element`처럼 문맥의 의미를 바꾸는 컨테이너만 의도적으로 새 `scroll_offset`을 구성한다. 가로 `strip_element`는 축 없는 scroll offset을 재사용하지 않고 기존 값을 자식에게 전달한다.

## Element의 arranged 상태

`ui_element::set_bounds()`가 호출되면 element의 `arranged()`가 참이 된다. Bounds 크기가 0인 것과 arrange되지 않은 것은 다르다. 조건에 따라 0 크기를 받는 element도 정상적으로 arrange된 상태일 수 있다.

보이는 element의 `arrange()` 구현은 자기 bounds를 설정하고 배치할 자식마다 `arrange()`를 호출해야 한다. 그리기와 hit test는 설정된 bounds를 사용한다.

## 표준 tree 생성

Root가 자식을 모두 배치하는 tree는 다음과 같이 만든다.

```cpp
auto root = make_popup_content();
auto tree = luil::make_arranged_tree(
    std::move(root),
    { 0.0f, 0.0f, popup_width, popup_height },
    scale);
```

`make_arranged_tree()`는 null이 아닌 root에 `arrange({ slot, scale })`를 한 번 호출한 뒤 `ui_tree`로 감싼다. Null root는 빈 tree로 통과시킨다.

Popup, menu, panel, stack처럼 root 하나가 전체 계층을 배치하는 경우 이 진입점을 사용한다.

## 직접 조립하는 root

`root_element`처럼 caption과 content를 호출자가 별도 slot에 배치한 뒤 한 root에 담는 구성은 `ui_tree` 생성자를 직접 사용할 수 있다. 이 경우 각 가지를 빠짐없이 arrange하는 책임은 조립 코드에 있다.

```cpp
auto root = std::make_unique<luil::root_element>();
root->arrange({ root_slot, scale });
caption->arrange({ caption_slot, scale });
content->arrange({ content_slot, scale });
root->add(std::move(caption));
root->add(std::move(content));
auto tree = luil::ui_tree { std::move(root) };
```

두 생성 방식 모두 결과 tree에 대한 진단 계약은 같다.

## `unarranged()` 진단

`ui_tree` 생성자는 tree를 색인하면서 보이지만 `arranged() == false`인 element id를 `unarranged()`에 기록한다. 정상 tree에서는 이 목록이 비어 있어야 한다.

보이지 않는 element는 `unarranged()`에 포함하지 않는다. 그리지도 hit test하지도 않으며 조건부 UI가 배치되지 않은 채 tree에 남을 수 있기 때문이다.

진단은 누락을 자동 수정하지 않는다. Bounds를 임의로 추측하면 layout 오류를 숨기므로 테스트와 frame 구성 검증에서 실패 원인으로 사용한다.

`duplicate_ids()`도 같은 시점에 확인하는 것이 좋다. 입력 상태는 id로 frame 사이를 잇기 때문에 배치와 id 무결성을 함께 검사해야 한다.

## Container 구현 지침

새 container의 `arrange()`는 다음 순서를 따른다.

1. 자기 bounds를 `context.slot`로 설정한다.
2. 논리 길이에 유효한 scale을 적용한다.
3. 각 자식의 물리 slot을 계산한다.
4. `context.for_child(slot)`으로 자식을 배치한다.
5. 스크롤 viewport나 maximum 같은 arrange 결과를 저장한다.

숨긴 자식이 공간을 차지하는지 여부는 container 계약에서 명시한다. Stack과 wrap은 보이지 않는 자식의 공간을 유지한다.

## 반드시 유지할 불변식

- 보이는 element는 tree 게시 전에 arrange되어야 한다.
- `set_bounds()`가 arranged 상태의 유일한 설정 지점이다.
- 0 크기와 미배치는 서로 다른 상태다.
- 자식 문맥은 기본적으로 `for_child()`로 잇는다.
- Root 하나가 전체를 배치하면 `make_arranged_tree()`를 사용한다.
- `unarranged()`와 `duplicate_ids()`가 모두 빈 tree만 게시한다.

## 검증 지침

[ui_element_tests.cpp](../tests/ui_element_tests.cpp)는 문맥 전달, 미배치 진단, 숨김 제외, 표준 진입점, 빈 tree를 검증한다. 새 container test는 root와 모든 보이는 자식의 `arranged()`와 예상 bounds를 확인해야 한다.
