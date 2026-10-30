# 여러 줄 라벨

설명·오류 글은 칸의 폭에서 줄을 바꾸고, 정한 줄 수를 넘으면 마지막 줄 끝을 줄여 아래 내용을 밀어내지 않아야 한다. `label_element`는 한 줄이었고, 앱이 자체 element로 폭을 재 접고 마지막 줄을 줄였다.

공개 형식은 [label_element.h](../include/luil/ui/label_element.h)의 `label_config::wrap`·`max_lines`·`label_element::measure_wrap`이다.

## 규칙

- `wrap`이면 폭에서 줄을 바꾼다. 줄 바꿈은 여러 줄 칸(`text_area_element`)과 같은 text_layout 배치다 — 공백에서 먼저 바꾸고, 공백이 없는 긴 낱말은 **글자소** 경계에서 바꾼다. 조합형(NFD) 한글과 ZWJ로 이은 이모지가 줄 경계에서 갈리지 않는다. `\n`은 줄 바꿈이다.
- `max_lines`(0이면 무제한)를 넘으면 마지막 보이는 줄이 남은 글 전체를 맡아 한 줄로 펴고(줄 바꿈은 공백) 끝을 `…`로 줄인다. 말줄임 자리도 글자소 경계다(`elide_text`).
- 한 줄의 높이는 `height_for`다. 한 줄 label과 같은 행 높이가 줄마다 쌓인다 — 한 줄짜리 설명이 여러 줄이 되어도 첫 줄의 자리가 그대로다.
- `preferred_size`가 접힌 높이를 답한다. 줄이 바뀌었으면 폭은 받은 폭이고, 한 줄이면 글의 폭이다. 배치 컨테이너(`stack`의 맞춤 길이, `grid`)가 그 높이를 쓴다.
- 바꾼 줄도 줄마다 `align`을 따른다. `elide`(가운데 줄임)는 한 줄 label의 것이고, 넘친 마지막 줄은 늘 끝을 줄인다.
- 규칙은 직접 그리는 글과 하나다 ([직접 그리는 글](text-block-design.md)). `label_wrap`은 `text_block_layout`이다.
- 접근성 이름은 줄인 것과 상관없이 글 전체다.

`measure_wrap(config, context, width)`는 그리기·배치와 같은 규칙으로 바꾼 줄의 원문 구간·말줄임 여부·높이를 돌려준다. 칸 높이를 미리 정해야 하는 앱이 같은 답을 얻고, test가 줄 경계를 직접 본다.

## 검증

- `tests/label_wrap_tests.cpp`: 공백 우선 바꿈과 접힌 높이·폭, `\n`, `max_lines` 말줄임과 무제한, 조합형 한글·이모지 결합 글자의 글자소 경계, 배율에 상관없는 바꿈 자리.
- 데모 "버튼과 컨트롤" 페이지의 두 줄 설명 글(창 폭을 줄이면 마지막 줄 끝이 줄어든다).
