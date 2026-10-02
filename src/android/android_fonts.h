#pragma once

#include <string>

namespace luil::android {
    // 사용자 UI 언어(BCP-47, `ko-KR` 꼴)를 글꼴 자원에 알린다.
    // Android는 언어를 Activity의 구성(AConfiguration)으로만 알 수 있어, 앱 host가 시작할 때
    // 읽어 넣는다. 글꼴 registry는 이 값을 한 번만 읽으므로 첫 글꼴 조회보다 앞서야 한다.
    void set_user_language(std::string language);
} // namespace luil::android
