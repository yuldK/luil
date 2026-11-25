#pragma once

#include "luil/app/file_dialog.h"

#include <functional>

#ifdef __OBJC__
@class UIViewController;
#else
class UIViewController;
#endif

namespace luil::ios {
    // 파일 대화 상자 하나를 문서 선택기(UIDocumentPickerViewController)로 띄운다 (file-dialog-design.md의 iOS 갈래).
    //  - 비동기다. 사용자가 고르거나 그만두면 `done`이 UI thread(메인 thread)에서 한 번 불린다.
    //  - 열기(`open`·`open_multiple`)는 고른 파일을 앱의 임시 폴더로 **복사**한 경로를 준다. 앱이 보안 범위 없이 바로 읽는다.
    //    원본을 고쳐 쓸 수는 없다 (그것은 저장 대화 상자의 일이다).
    //  - 폴더(`folder`)는 원본 폴더의 경로다. 보안 범위 접근을 열어 두고 프로세스가 끝날 때까지 닫지 않는다.
    //  - 저장(`save`)은 iOS에 "이름을 정해 저장할 자리"를 고르는 선택기가 없어 폴더를 고르게 하고 그 안의 `default_name`
    //    (비었으면 "Untitled") 경로를 준다. 그 폴더도 보안 범위 접근을 열어 둔다. 같은 이름이 있으면 앱이 덮어쓴다.
    //  - 거르개는 확장자들을 하나로 묶는다 (macOS와 같다). 기본 폴더(`initial_folder`)를 따른다. 제목은 쓸 자리가 없다.
    void run_file_dialog(UIViewController* presenter, const file_dialog_request& request, std::function<void(file_dialog_result)> done);
}
