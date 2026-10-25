# 비밀 저장소

로그인 토큰·서버 암호처럼 앱이 다음 실행에도 들고 있어야 하는 비밀을 앱마다 직접 OS 저장소에 넣었다. Windows에서는 자격 증명 관리자 API나 DPAPI를, Android에서는 Keystore와 파일을 다뤘다. 이제 라이브러리가 한 API로 맡는다.

공개 형식은 [secret_store.h](../include/luil/app/secret_store.h)다.

```cpp
const auto saved { luil::store_secret(u8"my-app", u8"server/admin", token_bytes) };
std::vector<std::uint8_t> token {};
if (luil::load_secret(u8"my-app", u8"server/admin", token).status == luil::secret_store_status::ok)
    use(token);
static_cast<void>(luil::erase_secret(u8"my-app", u8"server/admin"));
```

## 계약

- 항목은 `service`(앱이나 기능의 묶음)와 `name`(계정·서버 같은 항목 이름)의 짝으로 가린다. 같은 짝에 다시 쓰면 덮는다.
- 값은 바이트다. 글을 넣으려면 UTF-8 바이트로 넣는다. 크기는 2560바이트까지다(Windows 자격 증명의 상한). 넘으면 두 플랫폼 모두 `failed`다. 한 플랫폼에서만 되는 크기를 두지 않으려는 것이다.
- 결과는 상태와 진단 글이다.
  - `ok`;
  - `not_found`(읽거나 지울 항목이 없다);
  - `unsupported`(이 플랫폼·구성에 저장소가 없다);
  - `failed`(OS가 거절했다. 글이 OS의 이유를 든다).
- 지울 항목이 없으면 `not_found`다. 앱이 "이미 없다"를 실패와 가를 수 있다.
- 동기 호출이다. Android에서는 Keystore 호출이 수십 ms까지 걸리므로 UI thread에서 부르지 않는다.
- 값은 이 기기·이 사용자에게만 열린다. 백업·동기화로 다른 기기에 옮겨 가는 것을 바라는 값은 이 저장소에 두지 않는다.

## Windows

자격 증명 관리자의 일반 자격 증명이다(`CredWriteW`·`CredReadW`·`CredDeleteW`, `CRED_TYPE_GENERIC`).

- 대상 이름은 `luil:<service>/<name>`이다. 사용자가 "자격 증명 관리자"에서 그 이름으로 보고 지울 수 있다. 앱이 직접 쓰던 DPAPI 파일은 어디 있는지 사용자에게 보이지 않았다.
- 보존은 `CRED_PERSIST_LOCAL_MACHINE`이다. 이 기기의 이 사용자에게만 남고, 도메인 로밍 프로필로 옮겨 가지 않는다.
- OS가 사용자 로그온 비밀로 값을 암호화한다. 같은 사용자의 다른 프로세스는 읽을 수 있다. 그것이 이 저장소의 경계다.

## Android

Android Keystore의 AES-256-GCM 키로 값을 암호화해 앱 내부 저장소의 파일에 둔다.

- 키는 `service`마다 하나다(별칭 `luil-secret:<service>`). Keystore 밖으로 나오지 않는다. 처음 쓸 때 짓는다(`KeyGenParameterSpec`, GCM·패딩 없음).
- 파일은 `<files>/luil-secrets/<service와 name의 16진>`이다. 판 번호 1바이트, IV 12바이트, 암호문과 태그 순이다. 임시 파일에 쓰고 이름을 바꿔, 쓰다 끊긴 반쪽 파일이 남지 않는다.
- **Java 0줄이다.** Keystore와 Cipher는 JNI로 프레임워크 클래스를 부른다. HTTP client가 `HttpURLConnection`을 부르는 길과 같다. 앱 클래스를 찾지 않으므로 어느 thread에서든 class loader 문제가 없다.
- 앱을 지우면 키와 파일이 함께 사라진다. 기기 백업에 파일이 실려 가도 키가 없어 다른 기기에서는 풀리지 않는다. 그때는 `failed`가 아니라 항목을 지우고 `not_found`로 답한다. 앱은 다시 로그인하라고 하면 된다.
- 판 번호가 모르는 값이거나 파일이 짧으면 `failed`다.

## 자동 완성 힌트는 미룬다

요청에는 텍스트 칸의 자동 완성 힌트(아이디·암호)와 Android 암호 관리자의 자동 완성이 함께 있었다. 이번에는 넣지 않는다.

- Android 암호 관리자는 Autofill 프레임워크로만 칸에 닿는다. 프레임워크는 View 계층의 가상 구조(`onProvideAutofillVirtualStructure`)를 읽는데, 그것을 내려면 View를 상속한 Java 클래스가 있어야 한다. 이 저장소의 Java 0줄 방침과 부딪힌다. Android 접근성을 같은 이유로 뺀 것과 같은 판단이다. 키보드 쪽(EditorInfo의 입력 종류)만으로는 암호 관리자가 칸을 찾지 못한다.
- Windows에는 그린 칸에 쓰는 OS 자동 완성 규약이 없다. 데스크톱 암호 관리자는 UI Automation으로 칸을 찾는다. luil은 이미 편집 역할·이름·`IsPassword`를 내준다.
- 그래서 힌트 필드만 더하면 읽는 쪽이 없는 API가 된다. Android 쪽 Java 방침이 바뀌면 그때 함께 넣는다.

## 검증

- `tests/secret_store_tests.cpp`(Windows): 시험용 service 이름으로 쓰기·읽기·덮어쓰기·지우기를 한다. 없는 항목, 상한을 넘는 값, 빈 이름도 본다. test가 쓴 자격 증명은 그 자리에서 지운다.
- Android는 JavaVM이 있어야 해서 단위 test로 돌지 않는다. 모바일 데모의 "기기" 페이지("한 바퀴 돌려 보기")에서 실기기로 확인한다. 쓰기·읽기·덮어쓰기·지우기·없는 항목을 차례로 돌려 결과를 화면에 적는다.
- 데모 "입력과 다이얼로그" 페이지의 "비밀 저장소 확인" 단추가 같은 차례를 Windows에서 돌려 결과를 옆 줄에 적는다.
