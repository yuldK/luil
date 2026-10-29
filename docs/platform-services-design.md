# 플랫폼 도구

앱마다 Win32를 직접 불러 해 오던 작은 일 일곱이다. 하나하나는 짧지만 바르게 하기는 어렵다. 쓰다 끊긴 설정 파일, 따옴표가 깨진 명령 줄, 해제하지 않은 뮤텍스, 붙들지 않은 thread 핸들 같은 실수가 앱마다 되풀이됐다. 공개 형식은 [platform_services.h](../include/luil/app/platform_services.h)다.

모든 함수가 `platform_result`를 돌려준다. 상태는 넷이다.

- `ok`;
- `not_found`(대상이 없다, 취소할 I/O가 없다, 또는 그 주소를 열 앱이 없다);
- `unsupported`(이 플랫폼에는 그 일이 없다);
- `failed`(OS가 거절했다. 글이 OS의 이유를 든다).

| 일 | Windows | Android |
| --- | --- | --- |
| 원자적 파일 교체 | 같은 폴더의 임시 파일에 write-through로 쓰고 `MoveFileExW(REPLACE_EXISTING \| WRITE_THROUGH)` | 같은 폴더의 임시 파일에 쓰고 `fsync` 뒤 `rename`, 폴더도 `fsync` |
| 단일 인스턴스 | 이름 있는 뮤텍스 `Local\luil-instance:<이름>` | 지원 안 함. OS가 앱 하나에 프로세스 하나를 둔다 — `primary()`는 참이다 |
| 파일 잠금 | `LockFileEx`(배타, 기다리지 않음) | `flock(LOCK_EX \| LOCK_NB)` |
| 파일 관리자에서 보이기 | `SHOpenFolderAndSelectItems`(그 파일을 고른 채로) | 지원 안 함 |
| 외부 프로그램 실행 | `CreateProcessW`(인자를 Windows 규칙으로 따옴표 처리) | 지원 안 함 |
| 주소 열기 | `ShellExecuteExW`(open, 대화 상자 없이 끝날 때까지) | JNI로 `ACTION_VIEW` Intent를 Application context에서 `startActivity` |
| 배경 우선순위 | 지금 thread의 `THREAD_MODE_BACKGROUND_BEGIN`·`END`(CPU·I/O·메모리 우선순위를 함께 내린다) | 지금 thread의 `setpriority` nice 10 / 0 |
| 막힌 동기 I/O 취소 | `CancelSynchronousIo`(지은 thread의 핸들을 붙든다) | 지원 안 함 |

## 원자적 파일 교체

`replace_file_atomically(path, bytes)`가 설정·목록 파일처럼 통째로 다시 쓰는 파일을 맡는다.

- 읽는 쪽은 언제나 옛 파일 전체나 새 파일 전체를 본다. 쓰다 끊기면(전원·강제 종료) 옛 파일이 그대로다.
- 임시 파일은 같은 폴더에 둔다. 다른 볼륨으로는 이름 바꾸기가 원자적이지 않다. 이름은 `<파일 이름>.<프로세스 id>.tmp`라 두 프로세스가 같은 파일을 써도 임시 파일이 겹치지 않는다. 실패하면 임시 파일을 지운다.
- write-through는 디스크 캐시를 지나 쓰는 것까지다. 장치 자체의 쓰기 캐시는 OS의 정책을 따른다.
- 폴더가 없으면 `failed`다. 만들지 않는다. 어디에 만들지는 앱이 안다.

## 단일 인스턴스·파일 잠금

`single_instance`와 `file_lock`은 객체가 사는 동안 쥐고, 없어질 때 놓는다. 프로세스가 죽어도 OS가 놓는다.

- `single_instance { u8"my-app" }.primary()`가 첫 인스턴스인지 답한다. 두 번째 인스턴스는 대개 첫 인스턴스에 창을 앞으로 세우라고 알리고 끝낸다. 알리는 길(IPC)은 앱마다 달라 넣지 않았다.
- 뮤텍스는 `Local\` 이름 공간이다. 같은 사용자 세션 안에서만 하나다. 빠른 사용자 전환으로 다른 세션에서 띄운 것은 따로다.
- `file_lock`은 파일 하나를 배타적으로 잠근다. 파일이 없으면 만든다. 이미 잠겨 있으면 기다리지 않고 `locked()`가 거짓이다. 같은 프로세스의 두 객체도 서로 막는다.

## 파일 관리자·외부 프로그램

- `reveal_in_file_manager(path)`는 탐색기 창을 열고 그 파일을 고른 채로 둔다. 폴더를 주면 그 폴더를 고른다. 없는 경로는 `not_found`다. 부르는 thread에서 COM을 apartment로 연다(이미 열려 있으면 그대로 쓴다).
- `launch_program(program, arguments, options)`은 인자 목록을 Windows의 명령 줄 규칙(따옴표 안의 백슬래시와 따옴표)으로 이어 넘긴다. 앱이 이어 붙인 명령 줄이 빈칸·따옴표 든 경로에서 깨지던 자리다. `options`는 셋이다.
  - `working_directory`;
  - `show_window`(거짓이면 콘솔 창을 띄우지 않는다);
  - `wait`(참이면 끝날 때까지 기다려 `exit_code`를 싣는다).

## 주소 열기

`open_url(url, options)`는 기본 브라우저·메일 앱으로 주소를 연다. 텍스트 칸의 하이퍼링크를 누르는 길이다.

### 허용 목록을 둔 이유

이 함수에 오는 주소는 대개 앱이 만든 것이 아니라 문서·채팅·웹 응답 같은 **글 안에 적힌 것**이다. 누르는 사람은 링크의 겉글만 보고, 주소는 남이 정한다. OS의 "열기"는 scheme에 따라 하는 일이 크게 다르다.

- `file:`과 경로(`C:\...\a.exe`)는 실행 파일을 띄운다. scheme이 없는 글도 Windows가 경로로 읽는다.
- `javascript:`·`vbscript:`나 앱이 등록한 사용자 scheme은 그 처리기에게 무엇이든 넘긴다.

그래서 연결 고리 쪽에서 막는다. `open_url_options::schemes`(기본 `http`·`https`·`mailto`)에 든 scheme만 연다. 앱이 자기 scheme을 열어야 하면 목록을 바꿔 넘긴다. 빈 목록은 아무것도 열지 않는다.

`url_scheme_allowed(url, schemes)`가 그 판정이고, 따로 불러 링크를 그릴지 정하는 데도 쓴다.

- scheme은 RFC 3986대로 읽는다: 알파벳 하나, 그 뒤 알파벳·숫자·`+`·`-`·`.`, 그리고 `:`. 대소문자를 가리지 않는다(목록 쪽도).
- scheme이 없거나 깨졌으면, `:` 뒤가 비었으면 거짓이다. `example.com/a:b`는 `/`가 먼저 나와 scheme이 없다.
- 공백으로 시작·끝나거나 제어 문자(C0·DEL·C1)가 섞이면 거짓이다. 브라우저와 OS는 이런 글자를 걸러 읽어 ` javascript:`·`java\tscript:`가 되살아난다. 고쳐 읽지 않고 막는다.

허용되지 않은 주소는 열지 않고 `failed`다. 열 앱이 없으면 `not_found`다.

### Windows

`ShellExecuteExW`에 `open` 동사와 주소를 넘긴다.

- `SEE_MASK_NOASYNC`: 메시지 루프가 없거나 곧 끝날 thread에서 불러도 넘기기가 끝난 뒤 돌아온다.
- `SEE_MASK_FLAG_NO_UI`: 처리기가 없을 때 shell의 대화 상자 대신 `ERROR_NO_ASSOCIATION`을 받아 `not_found`로 답한다.
- scheme 처리기가 COM으로 서는 shell 확장일 수 있어, 부르는 thread에서 COM을 apartment로 연다(문서의 권고다. 이미 열려 있으면 그대로 쓴다).

### Android

JNI로 `new Intent(Intent.ACTION_VIEW, Uri.parse(url))`을 짓고 `FLAG_ACTIVITY_NEW_TASK`와 `CATEGORY_BROWSABLE`을 달아 `Context.startActivity`를 부른다. Java 파일은 없다.

- Context는 Activity가 아니라 **Application**이다. GameActivity가 메인 thread에서 부르는 생성 함수(`main_thread.cpp`가 링크로 먼저 받는 자리)에서 `getApplicationContext()`의 전역 참조를 한 번 쥔다. Activity는 회전·종료로 지고 다시 서지만 Application은 프로세스와 함께 살아 참조가 낡지 않는다. Application context에서 띄우려면 새 task가 있어야 해서 `FLAG_ACTIVITY_NEW_TASK`를 단다.
- `CATEGORY_BROWSABLE`은 브라우저가 링크를 열 때 다는 것과 같다. 링크로 불려도 안전하다고 밝힌 앱만 받는다. 메일 앱도 브라우저의 `mailto:` 링크를 받으려 이것을 밝힌다.
- **부른 thread에서 바로** 띄운다(`jni_thread_scope`로 붙인다). `startActivity`는 system server로 가는 binder 호출이라 어느 thread에서나 된다. 메인 thread로 넘기지 않은 이유는 둘이다.
  - 넘기면 답을 돌려줄 수 없다;
  - 기다리면 UI thread와 메인 thread가 서로 막힐 수 있다. glue가 수명 주기 전환에서 메인 thread를 세우고 UI thread를 기다린다.
- 처리할 앱이 없으면 `startActivity`가 그 자리에서 `ActivityNotFoundException`을 던진다. 예외를 거두고 `not_found`로 답한다. 그 밖의 예외는 `failed`다.

### 한계

- Windows는 처리기에게 넘긴 데까지가 답이다. 브라우저가 페이지를 실제로 열었는지는 모른다. `SEE_MASK_NOASYNC` 때문에 넘기는 동안 부른 thread가 잠깐 멈춘다.
- Android 10부터 **앞에 보이지 않는 앱**이 Activity를 띄우면 OS가 예외 없이 조용히 막는다. 이때도 `ok`다. 사용자가 링크를 누른 순간처럼 앱이 앞에 있을 때 부른다.
- Android에서 Application context는 luil이 `GameActivity_onCreate`를 받을 때 쥔다. luil 밖에서 GameActivity를 링크했으면 없어서 `failed`다.
- 주소 자체(도메인·경로)는 보지 않는다. 피싱 도메인을 걸러 주지 않는다. 그것은 링크를 보여 주는 쪽(표시 글과 실제 주소를 함께 보이기)의 일이다.

## 배경 우선순위·I/O 취소

- `set_background_priority(true)`는 **부른 thread**를 배경으로 내린다(색인 작성·섬네일 짓기). 같은 thread에서 `false`로 되돌린다. 다른 thread를 내리는 길은 두지 않았다. Windows의 배경 모드가 자기 thread만 받는다.
- `blocking_io_canceller`는 지은 thread를 붙든다. 다른 thread에서 `cancel()`을 부르면 그 thread가 막혀 있는 동기 I/O(파일·파이프 읽기, 느린 네트워크 공유)를 끊는다. 막힌 I/O가 없으면 `not_found`다. 끊긴 쪽은 `ERROR_OPERATION_ABORTED`를 받는다. 취소는 한 번뿐이다. 다음 I/O가 다시 막히면 다시 부른다.

## Android에서 뜻이 없는 것

공개 API는 두 플랫폼에 같이 있고, 플랫폼 층에서만 갈린다. 뜻이 없는 것은 `unsupported`로 답한다.

- 단일 인스턴스: OS가 앱(패키지)마다 프로세스와 task를 하나로 묶는다. `primary()`는 늘 참이고 `status()`는 `unsupported`다.
- 파일 관리자·외부 프로그램: 다른 앱은 프로세스가 아니라 Intent로 열린다. 파일을 고른 채로 보여 주는 공통 파일 관리자도 없다. 주소는 예외다. `ACTION_VIEW`는 Java 코드 없이 JNI로 Application context에서 띄울 수 있어 `open_url`이 연다.
- 막힌 I/O 취소: 같은 일을 하는 시스템 호출이 없다. 신호로 끊는 길은 앱의 다른 시스템 호출까지 흔들어 넣지 않는다.

## 검증

- `tests/platform_services_tests.cpp`(Windows):
  - 원자적 교체의 내용과 남은 임시 파일, 없는 폴더;
  - 단일 인스턴스 둘과 놓은 뒤의 첫째, 파일 잠금 둘;
  - 명령 줄 따옴표 규칙, `cmd /c exit 3`의 종료 코드(창 없이);
  - 없는 경로의 보이기(탐색기를 열지 않는 갈래만);
  - `url_scheme_allowed` 표(허용·대소문자·`javascript:`·`file:`·경로·scheme 없음·앞뒤 공백·제어 문자·사용자 지정 목록·빈 목록), 막힌 주소의 `open_url`이 열지 않고 `failed`인 것(허용된 주소를 실제로 여는 갈래는 브라우저가 떠서 보지 않는다);
  - 배경 우선순위 왕복, 파이프 읽기에 막힌 thread의 취소와 막힌 것이 없을 때의 `not_found`.
- Android는 Catch2 test를 돌릴 진입점이 없다. 그래서 `platform_services_android.cpp`를 그대로 묶은 작은 확인 프로그램을 실기기에서 셸 사용자와 앱 uid(`run-as`)로 돌렸다. 원자적 교체(남는 임시 파일 없음, 없는 폴더는 실패), 파일 잠금 둘, 단일 인스턴스의 `unsupported`, 우선순위 nice 10 → 0 왕복, 나머지 셋의 `unsupported`가 모두 맞았다. `open_url`은 Android 빌드까지만 확인했다(실기기에서 Intent를 띄워 보지 않았다).
- 데모:
  - 창 배치 저장이 `replace_file_atomically`를 쓴다;
  - 데모가 시작할 때 `single_instance`를 쥐고, 둘째 인스턴스면 제목에 그 사실을 적는다;
  - "창" 페이지에 "탐색기에서 실행 파일 보기"·"메모장 열기" 단추가 있다.
