#include "luil/app/updater.h"

// core test 실행 파일은 플랫폼 계층을 링크하지 않는다 (core_platform_stub.cpp). 모바일 demo의 기기 페이지는 자동 업데이트의
// 적용 방식을 label로 보여, 그 페이지를 조립하는 test에 링크가 닿도록 모바일 데모가 도는 플랫폼(Android)의 답으로 채운다.
namespace luil {
    update_apply_mode platform_update_apply_mode() noexcept
    {
        return update_apply_mode::open_store;
    }
}
