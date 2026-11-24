#pragma once

#include "luil/app/file_dialog.h"

#include <functional>

#ifdef __OBJC__
@class NSWindow;
#else
class NSWindow;
#endif

namespace luil::macos {
    // 파일 대화 상자 하나를 연다 (NSOpenPanel·NSSavePanel). 창이 있으면 그 창의 sheet로, 없으면 앱 모달 창으로 띄운다.
    //  - 비동기다. 사용자가 고르거나 그만두면 `done`이 UI thread(메인 thread)에서 한 번 불린다.
    //  - 거르개는 확장자들을 하나로 묶는다. macOS 패널에는 거르개를 고르는 칸이 없다 (모든 거르개의 합을 받는다).
    //  - 기본 폴더(`initial_folder`)와 저장 이름(`default_name`)을 따른다. 제목은 패널의 안내 글(message)이다.
    void run_file_dialog(NSWindow* owner, const file_dialog_request& request, std::function<void(file_dialog_result)> done);
}
