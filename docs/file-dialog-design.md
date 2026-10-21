# 파일 대화상자

앱은 `ui_frame::file_dialog`에 UTF-8 요청을 싣고 `app_delegate::make_file_dialog_message`에서
결과를 앱 메시지로 바꾼다. 요청을 frame에서 빼면 이미 열린 OS 대화상자를 취소하지는 않는다.
앱 수명 동안 고유한 `id`를 쓴다. 같은 ID는 다른 요청을 거친 뒤 다시 게시해도 열리지 않는다.
OS modal loop가 frame 게시를 처리해도 겹쳐 열지 않고, 끝난 뒤 최신 frame을 다시 확인한다.

`owner_surface`가 비면 주 창, 이름이 있으면 해당 보조 창이나 팝업을 소유 창으로 쓴다.
사라진 소유 창은 `failed`다. Win32 UI thread의 COM 초기화와 창 수명 규칙을 그대로 쓴다.
단일 열기·복수 열기·저장·폴더 선택을 `IFileDialog` 계열에 연결한다. 필터 확장자는
`png`, `.png`, `*.png`, `*` 형식을 받는다. 저장은 OS 덮어쓰기 확인을 사용한다.

`chosen`은 UTF-8 절대 경로 목록, `cancelled`는 사용자 취소, `failed`는 오류 설명을 담는다.
Android는 UI를 열지 않고 `unsupported`를 같은 ID로 한 번 반환한다.
데모 이미지 페이지는 플랫폼 코드를 직접 실행하지 않고 이 요청·결과 API를 쓴다.

중복 ID·재진입은 플랫폼 독립 gate 테스트로 확인한다. Windows의 modal 실행, 보조 창 소유권,
취소·복수 경로·저장과 Android의 결과 전달은 해당 플랫폼에서 실행 검증해야 한다.
