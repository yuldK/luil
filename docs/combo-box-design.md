# 편집형 콤보

`combo_box_element`는 입력 칸과 후보 popup을 조립한다. root와 입력 칸은 같은 kind이며 owner는
각각 앱의 이름과 `:input` 접미사다. 정책의 `text_target_of`는 같은 편집 대상을 돌려준다.
후보 popup은 `suggestion_popup(anchor)`로 만들고 frame에 싣는다. 입력 초점은 popup 클릭에도 남는다.

입력·IME 상태를 반영한 뒤 `displayed_text()`로 `begin_combo_query`를 부르고, 반환 token을
`suggestion_request(token)`에 넘긴다. 앱의 비동기 결과는 `accept_combo_suggestions`로 받는다.
다른 token과 이미 완료된 token은 버린다. 기본 필터는 표시 글이 라벨에 포함되는지이며 `matches`로 바꿀 수 있다.
선택·확정·복원·열림·강조·스크롤은 다른 luil 요소처럼 앱 메시지다. 확정한 앱은 열림 상태도 닫는다.
`allow_custom=false`이면 후보 밖 확정을 `invalid`로 보내며 앱의 검증 메시지를 칸 아래에 표시한다.

↑↓·Page·Home·End와 Enter는 열린 후보의 동작이고 Esc는 조합·열림·편집 복원의 순서다.
조합 중 키는 IME에 맡긴다. 새 token을 요청한 동안에는 스피너를 표시한다.

## C0 연동

조상 전파는 Claude의 C0가 소유한다. 이 브랜치는 그 구현을 복제하지 않는다.
`set_key_handler`가 있으면 생성자가 자동 연결하고 `ancestor_keys_connected()`가 참이다.
현재 기준 코드에는 그 API가 없으므로 후보 상태·필터·popup·공개 `on_key`는 구현되어 있지만,
입력 칸에서 콤보 조상으로 이어지는 키 경로의 완료는 C0 병합 뒤 검증해야 한다.
C0가 없는 구성을 편집형 콤보 전체가 준비된 것으로 보고하지 않는다.
