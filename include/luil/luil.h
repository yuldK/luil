#pragma once

// 라이브러리 공개 API 전체를 한 번에 들이는 umbrella header다.
// 빌드 시간을 아끼려면 필요한 header만 골라 include해도 된다 —
// 어느 쪽이든 경로는 항상 `luil/` 접두로 시작한다.

#include "luil/version.h"

// 스레드 경계 채널이다.
#include "luil/messaging/channel.h"
#include "luil/messaging/envelope.h"
#include "luil/messaging/latest_slot.h"

// 도메인 무관 텍스트 규칙이다 (UTF-8 순회, 한 줄 편집 상태 기계).
#include "luil/text/text_edit.h"
#include "luil/text/utf8_text.h"

// 색·글꼴 선호 값과 팔레트 합성이다.

// view 무관 UI element 계층이다.

// Win32 platform 조립이다 (창 실행, 앱 계약).

// HTTP 요청·답이다 (값과 몸 판정, 그리고 WinHTTP 위의 비동기 client).
