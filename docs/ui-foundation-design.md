# 공용 UI 보강

가로 막대는 `scrollbar_config::axis`로 고른다. 기존 높이 필드는 가로일 때 폭을 뜻하며,
`thumb_start()`와 `thumb_length()`는 축과 무관한 물리 좌표다. 기본 축은 세로다.
가로 막대는 좌우 키와 x 좌표 끌기를 쓰고, 접근성은 RangeValue와 수평 방향을 알린다.

라벨의 `align`은 start·center·end이고 `elide`는 end·middle이다. 기본 경로의 그림은
그대로 둔다. 가운데 말줄임은 앞뒤 문자 경계를 보존하며 글자소 편집 보강 뒤에는 그 경계를 따른다.

`spinner_element`는 이름을 가진 값 없는 progress다. 보이고 배치된 동안만 16ms 뒤를 예고한다.
가운데 버튼의 원시 입력은 구분하지만 기본 controller는 클릭·초점·선택·끌기 상태를 바꾸지 않는다.

인계 계약은 [Codex 트랙](ui-gap-codex-track.md)의 X0을 따른다.
