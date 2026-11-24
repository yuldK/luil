#pragma once

#ifdef __OBJC__
#import <AppKit/AppKit.h>

// 개발용 입력 대본이다. 환경 변수 `LUIL_MACOS_INPUT_SCRIPT`에 파일 경로를 주면 창이 뜬 뒤 그 줄들을 차례로 실행한다.
// 화면 기록·손쉬운 사용 권한 없이 실제 AppKit 이벤트 길(NSEvent → view → host)을 밟아 보려는 것이다.
//  - 이벤트는 앱 자신의 큐에 넣는다 (`-[NSApplication postEvent:atStart:]`). 다른 앱에는 닿지 않는다.
//  - 좌표는 창 내용 영역의 점(논리 픽셀, 왼쪽 위 원점)이다.
//  - 정하지 않으면 아무 일도 없다.
//
// 줄 하나가 명령 하나다. `#`로 시작하는 줄과 빈 줄은 건너뛴다.
//   wait <ms>               — 기다린다
//   move <x> <y>            — 포인터를 옮긴다
//   click <x> <y> [right]   — 누르고 뗀다 (right면 오른쪽 단추)
//   drag <x1> <y1> <x2> <y2> — 누른 채 끌어 뗀다
//   scroll <x> <y> <dy>     — 휠 줄 단위로 굴린다 (양수가 위)
//   key <keycode> [cmd] [shift] [option] [control] — 가상 키 하나를 누르고 뗀다
//   type <글>               — 글자마다 키를 누른다 (입력기를 거친다)
//   marked <글>             — 입력기의 조합 글을 세운다 (`setMarkedText:`)
//   commit <글>             — 입력기가 글을 확정한다 (`insertText:`)
//   snapshot <png> [창 id]  — 창 내용(주 창은 웹뷰를 포함한 합성, 보조 창은 그 창의 frame)을 PNG로 적는다 (view가 `luilSnapshot:`에 답할 때)
//   capture <경로>          — 지금 창을 무손실 WebP로 남긴다 (`capture_request`)
//   record [<경로>]         — 경로가 있으면 녹화를 시작하고, 없으면 멈춰 애니메이션 WebP로 적는다 (`record_request`)
//   window <id>             — 다음 명령을 그 창(보조 창 id, `main`이면 주 창)에 보낸다
//   drop <x> <y> <경로>      — 파일 하나를 그 자리에 끌어 와 놓는다 (view의 끌어 놓기 길을 밟는다)
//   sheet [cancel|name <이름>|folder <경로>] — 창에 붙은 sheet(파일 패널)를 다룬다. 키 명령은 sheet가 있으면 그리로 간다
//   quit                    — 지금 창의 닫기 길을 밟는다 (`performClose:`)
void luil_run_input_script(NSWindow* window, NSView* view);
#endif
