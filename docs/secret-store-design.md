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
  - `not_found`(읽거나 지우거나 옮길 항목이 없다);
  - `unsupported`(이 플랫폼·구성에 저장소가 없다);
  - `failed`(입력이 잘못됐거나 OS가 거절했다. 진단 글이 이유를 담는다);
  - `already_exists`(Windows 이전에서 새 항목이 이미 있다).
- 지울 항목이 없으면 `not_found`다. 앱이 "이미 없다"를 실패와 가를 수 있다.
- 동기 호출이다. Android에서는 Keystore 호출이 수십 ms까지 걸리므로 UI thread에서 부르지 않는다.
- 값은 이 기기·이 사용자에게만 열린다. 백업·동기화로 다른 기기에 옮겨 가는 것을 바라는 값은 이 저장소에 두지 않는다.

## Windows

자격 증명 관리자의 일반 자격 증명이다(`CredWriteW`·`CredReadW`·`CredDeleteW`, `CRED_TYPE_GENERIC`).

- 새 대상 이름은 `luil-v2:<service의 16진>-<name의 16진>`이다. 각 이름의 UTF-8 바이트를 소문자 16진수로 옮긴다. 슬래시, 대소문자, 이름 안의 NUL을 포함한 바이트 차이가 자격 증명 관리자에서도 구분된다.
- 두 이름 모두 비어 있지 않은 유효한 UTF-8이어야 한다. 인코딩한 대상이 Windows 자격 증명 이름 상한을 넘으면 `failed`다. 사용자는 자격 증명 관리자에서 새 대상 이름을 보고 지울 수 있다.
- 보존은 `CRED_PERSIST_LOCAL_MACHINE`이다. 이 기기의 이 사용자에게만 남고, 도메인 로밍 프로필로 옮겨 가지 않는다.
- OS가 사용자 로그온 비밀로 값을 암호화한다. 같은 사용자의 다른 프로세스는 읽을 수 있다. 그것이 이 저장소의 경계다.

### Windows 기존 항목 이전

옛 `luil:<service>/<name>`에는 슬래시와 대소문자 때문에 여러 이름 쌍이 들어갈 수 있었고, NUL 뒤가 잘렸다. 일반 쓰기·읽기·삭제는 새 대상에만 적용된다.

옛 항목의 소유권을 확인한 앱은 `migrate_legacy_win32_secret(service, name)`으로 원래 이름 쌍의 값을 새 키로 복사할 수 있다. 옛 버전과 다른 프로세스의 쓰기·삭제를 멈춘 상태에서 호출한다. 복사 성공은 `ok`, 원본이 없으면 `not_found`, 새 대상에 값이 이미 있으면 `already_exists`다. `ok`와 `already_exists` 뒤에는 일반 `load_secret`으로 새 값을 읽을 수 있다. 소유권을 확인할 수 없는 앱은 로그인으로 새 값을 받는다. Windows 이외에서는 `unsupported`다.

앱은 업그레이드 단계에서 복사를 한 번 수행하고 완료 여부를 기록한다. 복사와 재시도 중에 원본을 보존하므로 로그아웃 뒤 다시 복사하면 옛 값이 되살아난다.

## Android

Android Keystore의 AES-256-GCM 키로 값을 암호화해 앱 내부 저장소의 파일에 둔다.

- 키는 `service`마다 하나다(별칭 `luil-secret:<service>`). Keystore 밖으로 나오지 않는다. 처음 쓸 때 짓는다(`KeyGenParameterSpec`, GCM·패딩 없음).
- 파일은 `<files>/luil-secrets/<service와 name의 16진>`이다. 판 번호 1바이트, IV 12바이트, 암호문과 태그 순이다. 임시 파일 쓰기와 닫기가 끝난 뒤 이름을 바꾼다. 쓰기, 닫기 또는 이름 바꾸기가 실패하면 기존 파일을 보존하고 임시 파일 정리를 시도한다.
- **Java 0줄이다.** Keystore와 Cipher는 JNI로 프레임워크 클래스를 부른다. HTTP client가 `HttpURLConnection`을 부르는 길과 같다. 앱 클래스를 찾지 않으므로 어느 thread에서든 class loader 문제가 없다.
- 앱을 지우면 키와 파일이 함께 사라진다. 기기 백업에 파일이 실려 가도 키가 없어 다른 기기에서는 풀리지 않는다. 그때는 `failed`가 아니라 항목을 지우고 `not_found`로 답한다. 앱은 다시 로그인하라고 하면 된다.
- 판 번호가 모르는 값이거나 파일이 짧으면 `failed`다.

## macOS

로그인 keychain의 일반 암호 항목이다(`SecItemAdd`·`SecItemUpdate`·`SecItemCopyMatching`·`SecItemDelete`, `kSecClassGenericPassword`).

- `service`가 `kSecAttrService`, `name`이 `kSecAttrAccount`다. 바꾸지 않고 넣으므로 사용자가 Keychain Access에서 그 이름으로 보고 지울 수 있다. 대소문자와 슬래시는 그대로 구분된다.
- 두 이름 모두 비어 있지 않은 유효한 UTF-8이어야 하고 NUL을 품으면 안 된다. Keychain은 속성을 NUL에서 잘라 `name\0a`와 `name\0b`가 한 항목이 된다. 남의 값을 조용히 덮지 않게 쓰기 전에 `failed`로 거절한다. Windows와 다른 한 곳이다.
- 같은 짝에 다시 쓰면 `SecItemUpdate`로 값만 바꾼다. 지우고 새로 만들지 않아 항목의 접근 목록이 그대로다.
- 파일 기반 keychain이다. 데이터 보호 keychain(`kSecUseDataProtectionKeychain`)은 keychain-access-groups 서명 권한이 있어야 해서, 서명하지 않은 개발 빌드와 test 실행 파일에서 실패한다.
- 항목은 기본 접근 목록으로 만든다. 만든 프로그램이 믿는 앱으로 들어가 그 프로그램은 묻는 창 없이 읽고 고치고 지운다. 다시 빌드해 서명(ad-hoc 포함)이 바뀐 실행 파일이 옛 항목을 읽으면 OS가 허락을 묻는다. 접근 목록을 라이브러리가 넓히지 않는다.
- `migrate_legacy_win32_secret`은 `unsupported`다. 옮길 옛 항목이 없다.

## 자동 완성 힌트는 미룬다

요청에는 텍스트 칸의 자동 완성 힌트(아이디·암호)와 Android 암호 관리자의 자동 완성이 함께 있었다. 이번에는 넣지 않는다.

- Android 암호 관리자는 Autofill 프레임워크로만 칸에 닿는다. 프레임워크는 View 계층의 가상 구조(`onProvideAutofillVirtualStructure`)를 읽는데, 그것을 내려면 View를 상속한 Java 클래스가 있어야 한다. 이 저장소의 Java 0줄 방침과 부딪힌다. Android 접근성을 같은 이유로 뺀 것과 같은 판단이다. 키보드 쪽(EditorInfo의 입력 종류)만으로는 암호 관리자가 칸을 찾지 못한다.
- Windows에는 그린 칸에 쓰는 OS 자동 완성 규약이 없다. 데스크톱 암호 관리자는 UI Automation으로 칸을 찾는다. luil은 이미 편집 역할·이름·`IsPassword`를 내준다.
- 그래서 힌트 필드만 더하면 읽는 쪽이 없는 API가 된다. Android 쪽 Java 방침이 바뀌면 그때 함께 넣는다.

## 검증

- `tests/secret_store_tests.cpp`(Windows): 시험 계정용 키로 읽기·쓰기·덮어쓰기·삭제, 이름의 충돌 방지와 옛 키의 명시적 복사를 검증한다. 테스트가 쓴 자격 증명은 그 자리에서 지운다.
- `tests/android_jni_order_tests.cpp`(Android): 가짜 JNI와 `/dev/full`을 사용해 닫기 중 쓰기가 실패했을 때 기존 파일이 보존되고 임시 파일이 정리되는지 검증한다. 모바일 데모의 "기기" 페이지("한 바퀴 돌려 보기")에서 실제 Keystore로 쓰기·읽기·덮어쓰기·지우기·없는 항목을 확인한다.
- `tests/secret_store_tests.cpp`(macOS, `luil_macos_platform_tests`): 같은 파일이 실제 keychain으로 읽기·쓰기·덮어쓰기·삭제·상한·빈 값, 슬래시와 대소문자의 구분, NUL 이름과 잘못된 UTF-8의 거절, 이전의 `unsupported`를 본다. 묻는 창 없이 돈다.
- 데모 "입력과 다이얼로그" 페이지의 "비밀 저장소 확인" 단추가 같은 차례를 Windows에서 돌려 결과를 옆 줄에 적는다.
