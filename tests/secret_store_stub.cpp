#include "luil/app/secret_store.h"

// core test 실행 파일은 플랫폼 계층을 링크하지 않는다 (core_platform_stub.cpp). 모바일 demo의
// 기기 페이지는 비밀 저장소를 부르는 단추가 있어, 그 페이지를 조립하는 test에 링크가 닿도록
// 저장소가 없는 플랫폼의 답("지원 안 함")으로 채운다. 이 test들은 단추를 누르지 않는다.
namespace luil {
    secret_store_result store_secret(std::u8string_view, std::u8string_view, std::span<const std::uint8_t>)
    {
        return { secret_store_status::unsupported, {} };
    }

    secret_store_result load_secret(std::u8string_view, std::u8string_view, std::vector<std::uint8_t>& secret)
    {
        secret.clear();
        return { secret_store_status::unsupported, {} };
    }

    secret_store_result erase_secret(std::u8string_view, std::u8string_view)
    {
        return { secret_store_status::unsupported, {} };
    }
}
