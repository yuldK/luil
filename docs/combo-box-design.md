# 편집형 콤보

`combo_box_element`는 입력 칸과 후보 popup을 조립한다. root와 입력 칸은 같은 kind이며 owner는
각각 앱의 이름과 `:input` 접미사다. 정책의 `text_target_of`는 같은 편집 대상을 돌려준다.
후보 popup은 `suggestion_popup(anchor)`로 만들고 frame에 싣는다. 입력 초점은 popup 클릭에도 남는다.

입력·IME 상태를 반영한 뒤 `displayed_text()`로 `begin_combo_query`를 부르고, 반환 token을
`suggestion_request(token)`에 넘긴다. 앱의 비동기 결과는 `accept_combo_suggestions`로 받는다.
다른 token과 이미 완료된 token은 버린다. `suggestion_request`는 상태의 token과 같으면 그 token의 `query`를 보낸다.
새 질의를 시작해도 앞 후보는 새 결과가 올 때까지 남아 강조·↑↓·Enter가 이어진다.
다 받은 후보는 표시 글이 그 `query`와 같으면 원천의 답으로 보고 다시 거르지 않는다. 받는 중이거나 글이
그 뒤 바뀌었으면 기본 필터로 표시 글이 라벨에 포함되는(대소문자 구분) 후보만 남긴다. `matches`를 주면 언제나 그것으로 거른다.
선택·확정·복원·열림·강조·스크롤은 다른 luil 요소처럼 앱 메시지다. 확정한 앱은 열림 상태도 닫는다.
`allow_custom=false`이면 후보 밖 확정을 `invalid`로 보내며 앱의 검증 메시지를 칸 아래에 표시한다.
받는 중에는 남은 앞 후보와 정확히 같은 글만 선택하고, 아니면 `invalid` 없이 Enter를 삼킨다.

↑↓·Page와 Enter는 열린 후보의 동작이고 Esc는 조합·열림·편집 복원의 순서다.
Home·End(Shift 포함)는 열려 있어도 글 칸의 caret을 옮긴다. 편집형 콤보의 관례다.
조합 중 키는 IME에 맡긴다. 새 token을 요청한 동안에는 스피너를 표시한다.

## 조상 전파 연동

조상 전파(C0, [key-propagation-design.md](key-propagation-design.md))는 main에 들어와 있다. 생성자가
`set_key_handler`로 `on_key`를 연결하므로 입력 칸이 쓰지 않은 키가 콤보로 온다. `ancestor_keys_connected()`는
늘 참이며 옛 호출부를 위해 남겼다. 데스크톱 demo의 변경 작업대가 작성자 칸으로 이 경로를 쓴다.
