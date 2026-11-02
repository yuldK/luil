#include "mobile_demo/assets.h"

// core test 실행 파일은 앱 빌드가 만드는 견본 바이트 소스(mobile_demo.cmake의 embed_binary)를 링크하지 않는다.
// 모바일 데모의 기본 페이지는 그림 여섯 기호를 부르므로, 그 페이지를 조립하는 test에 링크가 닿도록 빈 바이트로 채운다.
// 크기가 0이라 디코더는 오류로 끝나고 페이지는 그 오류를 이미 다룬다 (`load_demo_picture`, `read_films`). 이 test들은 그림 칸을 읽지 않는다.
// 배열 길이는 0일 수 없어 한 바이트를 두되 크기는 0이다. 선언이 먼저 와야(assets.h) 정의가 외부 연결을 갖는다.
const std::uint8_t mobile_demo_gradient_png[1] {};
const std::size_t mobile_demo_gradient_png_size { 0 };
const std::uint8_t mobile_demo_sweep_webp[1] {};
const std::size_t mobile_demo_sweep_webp_size { 0 };
const std::uint8_t mobile_demo_spinner_gif[1] {};
const std::size_t mobile_demo_spinner_gif_size { 0 };
