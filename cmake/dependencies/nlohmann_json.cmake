include_guard(GLOBAL)

# nlohmann/json은 header-only라 빌드할 것이 없다.
# HTTP 응답의 JSON 본문이 이 타입으로 나가므로(`luil/net/http_body.h`) 공개 헤더가
# 이 include 경로를 요구한다 — test 전용인 Catch2와 달리 라이브러리 구성마다 필요하다.
#  - 공개 헤더가 들이는 것은 `<nlohmann/json_fwd.hpp>` 하나다. 본체는 값을 실제로
#    읽는 곳에서 들인다 — umbrella 헤더 하나가 2만 줄을 끌고 오지 않는 길이다.
#  - upstream의 CMakeLists는 들이지 않는다. 그쪽이 만드는 target·install 규칙은
#    소비자의 솔루션에 끼어들고, 우리가 필요한 것은 include 경로 하나뿐이다.
#  - 설치본은 이 헤더를 include/nlohmann/으로 함께 복사한다 (cmake/install.cmake).
#    그래서 imported target의 include 경로 하나로 소비자가 같은 헤더를 본다.
function(luil_find_nlohmann_json)
    set(single_include "${LUIL_NLOHMANN_JSON_ROOT}/single_include")
    if(NOT EXISTS "${single_include}/nlohmann/json.hpp")
        message(FATAL_ERROR
            "The nlohmann/json submodule is not initialized: ${LUIL_NLOHMANN_JSON_ROOT}\n"
            "Run: git submodule update --init third_party/nlohmann_json")
    endif()

    # 제3자 헤더라 경고 정책 밖이다 (SYSTEM → /external:I, Skia와 같은 처리).
    add_library(luil_nlohmann_json INTERFACE)
    target_include_directories(luil_nlohmann_json SYSTEM INTERFACE "${single_include}")
    add_library(luil::nlohmann_json ALIAS luil_nlohmann_json)
endfunction()
