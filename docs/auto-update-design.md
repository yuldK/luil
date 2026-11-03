# 자동 업데이트

앱이 새 판이 있는지 묻고, 받고, 받은 것이 진짜인지 확인하고, 적용하는 길이다. 서버에 둔 서명한 매니페스트를 받아 ECDSA P-256 서명을 OS 암호 API로 확인하고, 실행 파일을 덩이로 나눠 받으며 SHA-256을 맞춘다. 적용은 Win32에서 새 exe가 도우미로 떠 옛 exe를 바꾸고 다시 띄우며, Android에서 스토어 페이지를 연다. 이 문서는 확인·신뢰·내려받기와 공개 형식의 몸통을 맡고 적용은 [자동 업데이트 적용](auto-update-apply-design.md)이 맡는다. 공개 형식은 새 헤더 `include/luil/app/updater.h`의 `updater`·`updater_config`·`update_key`·`update_status`(`update_phase`·`update_failure`·`update_offer`)와 적용 문서의 `update_apply_options`·`update_apply_result`·`platform_update_apply_mode`·`run_update_helper`다.

## 문제

- 새 판을 묻고 견주는 코드가 없다. 매니페스트 형식도 판 비교도 없다. [version.h](../include/luil/version.h)는 luil 자신의 판이다.
- 받은 것을 믿을 길이 없다. 저장소의 암호 호출은 앱 식별이 이름 기반 UUID를 짓는 CryptoAPI SHA-1뿐이다([app_identity.cpp](../src/win32/app_identity.cpp) 27·43줄). SHA-256·ECDSA 확인이 없고 Win32 링크 목록에 `bcrypt`가 없다([src/CMakeLists.txt](../src/CMakeLists.txt) 736~760줄, `advapi32`는 743줄). HTTPS만 믿으면 서버나 배포 경로가 뚫릴 때 갱신 길이 곧 남의 코드를 돌리는 길이다.
- 큰 파일을 받을 길이 마땅치 않다. `http_client`는 몸 전체를 메모리 벡터 하나로 받고 기본 상한이 64 MiB다([http_message.h](../include/luil/net/http_message.h) 85·108·120줄). 진행 알림과 파일로 받기가 없고 답은 끝나야 한 번 온다(`deliver`, [http_client.h](../include/luil/net/http_client.h) 14~35줄). 끊기면 처음부터다.
- 자기 exe를 바꾸는 길이 없다. `replace_file_atomically`는 같은 폴더 임시 파일과 `MoveFileExW(REPLACE_EXISTING)`이고([platform_services_win32.cpp](../src/win32/platform_services_win32.cpp) 168줄) 도는 exe 위에는 덮어쓰지 못한다고 알려져 있다. `launch_program`은 손잡이를 넘기지 않고(416줄) 띄운 자식만 기다린다(426~432줄). 이 몫은 적용 문서가 푼다.
- 그래서 앱이 모두 짠다. OS 암호 API, 덩이 받기와 이어받기, 도우미 프로세스, 되돌리기, 단일 인스턴스와의 차례를 앱마다 다시 푼다. 한 곳이 틀리면 사용자 PC의 앱이 뜨지 않거나 남의 코드가 돈다.

## 공개 형식

`include/luil/app/updater.h`, namespace `luil`. core 검사 범위라([verify_core_portability.cmake](../cmake/verify_core_portability.cmake) 23~29줄) OS 헤더도 `luil/net/` 헤더도 nlohmann 형도 없다. 표준 헤더만 쓰고 `luil.h`가 include한다. 구현은 플랫폼 쪽 `luil` target에 있다. [platform_services.h](../include/luil/app/platform_services.h)가 앱 계약을 선언하고 Win32·Android가 구현하는 것과 같다. 적용 쪽 선언은 적용 문서에 있다.

```cpp
namespace luil {
    // 앱에 박는 서명 확인 키다 (결정 2). 키를 바꾸는 동안 둘 이상을 싣는다.
    struct update_key
    {
        std::u8string id {};          // 서명 항목의 `key`와 맞춘다
        std::u8string spki_base64 {}; // P-256 공개 키의 SubjectPublicKeyInfo DER을 base64로 (PEM 몸통 한 줄)
    };

    enum class update_phase { idle, checking, up_to_date, available, downloading, verifying, ready, applying, handed_off, failed };
    enum class update_failure
    {
        none, network, http_status, manifest_invalid, signature_invalid, manifest_mismatch, replayed, no_asset,
        too_large, range_unsupported, hash_mismatch, disk, busy, not_writable, launch_failed, unsupported, cancelled, stopped,
    };

    // 서명을 지난 매니페스트가 이 플랫폼에 내준 판이다.
    struct update_offer
    {
        std::u8string version {};
        std::u8string notes {};     // 서버가 준 글이다. 앱이 그대로 보인다
        std::u8string notes_url {}; // https만 (주소 규칙)
        std::uint64_t size { 0 };   // Win32가 받을 바이트. Android는 0
        std::u8string store_url {}; // Android가 열 주소. Win32는 비었다
    };

    struct update_status
    {
        update_phase phase { update_phase::idle };
        update_failure failure { update_failure::none }; // failed일 때만 뜻이 있다
        std::optional<update_offer> offer {};            // available부터 선다
        std::uint64_t received { 0 };                    // downloading·verifying의 진행이다
        std::uint64_t total { 0 };
        int http_status { 0 };                           // http_status 실패의 상태 코드다
        std::u8string error {};                          // 개발자가 읽을 영문 진단이다
    };

    struct updater_config
    {
        std::u8string app_id {};              // 매니페스트의 `app`과 같아야 한다
        std::u8string manifest_url {};        // 채널마다 하나다
        std::u8string channel { u8"stable" }; // 매니페스트의 `channel`과 같아야 한다
        std::u8string current_version {};     // 지금 판이다. 규칙(결정 3) 밖이면 create가 실패한다
        std::vector<update_key> keys {};      // 비면 create가 실패한다
        std::u8string download_directory {};  // 앱이 정하는 사용자 폴더다. 없으면 만든다
        std::uint64_t max_asset_bytes { 512u * 1024u * 1024u };
        std::u8string user_agent { u8"luil" };
        bool use_system_proxy { true };
        std::vector<std::u8string> store_schemes { u8"https", u8"market" }; // Android 적용이 열어도 되는 scheme
        // 단계가 바뀔 때와 받은 덩이마다 updater의 thread에서 불린다 (`http_client_config::deliver`와 같은 계약).
        std::function<void(const update_status&)> report {};
    };

    class updater
    {
    public:
        // 실패하면 null이고 `error`에 까닭을 적는다 (`http_client::create`와 같다). 지난 적용의 결과를 여기서 읽는다.
        [[nodiscard]] static std::unique_ptr<updater> create(updater_config configuration, std::u8string& error);
        ~updater(); // stop()

        // 어느 thread에서나 부른다. 받아들이면 참이고 결과는 `report`로 온다. 일하는 중이거나 단계가 맞지 않으면 거짓이다.
        [[nodiscard]] bool check(std::u8string& error);
        [[nodiscard]] bool download(std::u8string& error); // available에서. Android는 거짓이다 (unsupported)
        [[nodiscard]] bool apply(const update_apply_options& options, std::u8string& error); // 적용 문서
        void cancel() noexcept; // 하던 일을 접고 cancelled로 알린다. 받던 파일은 남아 다음 download가 잇는다
        void stop() noexcept;   // 멱등이다. 돌아온 뒤 `report`는 불리지 않는다
        [[nodiscard]] std::optional<update_apply_result> last_apply() const;
    };
}
```

서버에 두는 파일은 봉투 하나다(결정 1). 서명은 `payload`의 base64를 푼 바이트 그대로를 덮는다. 다시 쓰는 정규화가 없다.

```json
{ "format": "luil-update-envelope/1",
  "payload": "<매니페스트 JSON의 base64>",
  "signatures": [ { "key": "2026-a", "der": "<base64>" }, { "key": "2027-a", "raw": "<r||s 64바이트의 base64>" } ] }
```

```json
{ "format": 1, "app": "com.example.editor", "channel": "stable", "sequence": 42, "version": "1.4.0",
  "notes": "...", "notes_url": "https://example.com/editor/1.4.0",
  "assets": {
    "windows-x64": { "url": "editor-1.4.0.exe", "size": 12582912, "sha256": "<소문자 16진 64자>" },
    "android": { "store_url": "https://play.google.com/store/apps/details?id=com.example.editor" } } }
```

비공개 파일은 셋으로 갈린다. core의 `src/host/update_manifest.h`·`.cpp`는 OS 없이 값만 다룬다. 봉투와 매니페스트 읽기(nlohmann, core에 PRIVATE로 이미 링크된 것, src/CMakeLists.txt 700~703줄), 엄격한 base64, 판 읽기와 비교, 이 빌드의 플랫폼 칸 고르기(`windows-x64`·`windows-arm64`·`android`), asset 이름 풀기, 서명 꼴 바꾸기(raw와 DER), SPKI 머리 확인, 상태 파일 `update-state.json` 읽기·쓰기, 도우미 인자와 결과 파일(적용 문서)이다. `src/net/updater.cpp`는 상태 기계이고 자기 `http_client`를 하나 쥔다. 두 플랫폼 묶음에 같이 든다(`luil/url_scheme.cpp`처럼). `src/net/update_crypto.h`는 SHA-256 증분 해시와 P-256 서명 확인의 seam이고 `src/win32/update_crypto_win32.cpp`·`src/android/update_crypto_android.cpp`가 구현한다.

## 규칙

**흐름.** 앱이 부르고 updater가 단계를 알린다.

| 부른 것 | 단계 | 끝 |
| --- | --- | --- |
| `check` | `checking` | `up_to_date`, `available`, 받아 둔 같은 파일이 있으면 `verifying` 뒤 `ready`, `failed` |
| `download` (Win32) | `downloading`, `verifying` | `ready`, `failed` |
| `apply` | `applying` | `handed_off`, `failed` (적용 문서) |

- 확인 주기, 받기를 바로 할지 물을지, 적용을 지금 할지 끝날 때 할지는 앱이 정한다. 라이브러리는 스스로 확인하거나 받지 않는다. 일은 한 번에 하나다. `report`는 updater가 쥔 `http_client`의 thread에서 불린다. 짧아야 하고 앱은 `post_app_message`로 나른다. 종료 순서는 `http_client`와 같다. `logic_driver::cancel()`에서 `cancel()`, `stop_workers()`에서 `stop()`이다.

**확인.** 차례대로 보고 먼저 걸린 것이 답이다.

1. `manifest_url`을 GET한다. `parse.assume_kind = bytes`, `max_body_bytes`는 1 MiB다. 전송 오류는 `network`, 2xx가 아니면 `http_status`다.
2. 봉투를 읽는다. `format`, `payload`, 1~8개의 `signatures`가 없거나 꼴이 틀리면 `manifest_invalid`다.
3. 서명을 본다. 차례대로 `key`가 `keys`에 있는 항목만 확인하고 하나라도 맞으면 지난다. 없으면 `signature_invalid`이고 진단이 "아는 키가 없다"와 "맞지 않는다"를 가른다. **payload는 서명을 지난 뒤에만 JSON으로 읽는다.**
4. payload를 읽는다. 칸이 없거나 꼴이 틀리면 `manifest_invalid`, `app`·`channel`이 config와 다르면 `manifest_mismatch`, `sequence`가 상태 파일의 값보다 작으면 `replayed`다(결정 4). 지나면 `sequence`를 적는다. 모르는 칸은 넘긴다(뒤 판 호환).
5. 판을 견준다. `version`이 `current_version`보다 크고 되돌림으로 건너뛴 판(적용 문서 결정 9)이 아니면 `available`이다. 같거나 낮으면 `up_to_date`다. 낮은 판은 어떤 경우에도 내주지 않는다.
6. 이 빌드의 플랫폼 칸이 없으면 `no_asset`, `size`가 `max_asset_bytes`보다 크면 `too_large`다. Win32는 실행 파일 폴더에 쓸 수 있는지 보고 막혔으면 `not_writable`이다(적용 문서 결정 10). 이때도 `offer`를 실어 앱이 다른 길(`notes_url`)을 보인다.

**주소.** 매니페스트와 asset은 https다. http는 되돌이 host(`127.0.0.1`·`::1`·`localhost`)만 받는다(test와 데모). 서명이 내용을 지키지만 평문은 이 PC가 어느 판인지 드러낸다. asset `url`은 절대 주소이거나 `/`·`?`·`#`·`:`가 없는 이름 하나다. 이름이면 매니페스트 답의 `final_url`에서 마지막 경로 조각을 그 이름으로 바꾼다. 재지정은 `http_client` 규칙 그대로다(https에서 http로 따라가지 않는다).

**받기.** 결정 5의 길 a다.

- 4 MiB 덩이를 하나씩 받는다. 요청마다 `Range: bytes=<시작>-<끝>`, `decompress = false`(바이트 자리가 압축으로 어긋나지 않게), `max_body_bytes`는 덩이 크기, `assume_kind = bytes`다. `Range`는 다른 출처로 재지정돼도 남는 헤더다(http_message.h 52~56줄).
- 답은 206이고 `Content-Range: bytes <시작>-<끝>/<size>`가 요청과 매니페스트 크기에 맞아야 한다. 200은 몸이 매니페스트 크기와 같을 때만 받는다(덩이보다 작은 파일). 그 밖은 `range_unsupported`다.
- `<download_directory>/<sha256>.part`에 이어 쓰고 증분 SHA-256에 먹인다. 덩이마다 `received`·`total`을 알린다. 덩이 쓰기와 해시는 `deliver` 안에서 한다. 이 client는 updater만 써서 밀릴 다른 답이 없고 덩이 하나는 `stop_budget`(1초) 안에 끝난다.
- 다 받으면 `verifying`이다. 해시가 매니페스트와 같으면 디스크까지 내리고 `<sha256>.exe`로 이름을 바꿔 `ready`다. 다르면 `.part`를 지우고 `hash_mismatch`다. 쓰기·이름 바꾸기 실패는 `disk`다.
- 끊김·취소·종료에는 `.part`를 남긴다. 다음 `download`는 남은 바이트를 해시에 다시 먹인 뒤 그 길이부터 잇는다. 이름이 해시라 서버가 새 파일을 내면 새 이름이고 섞이지 않는다. 매니페스트를 고치지 않고 파일만 바꾼 서버는 끝의 해시가 잡는다.
- 받기와 적용 동안 `<download_directory>/update.lock`을 `file_lock`으로 쥔다. 같은 앱 둘이 같은 폴더를 함께 쓰지 않는다. 잠겨 있으면 `busy`다.

**진행 표시.** 라이브러리는 작업 표시줄을 만지지 않는다. 앱이 `ui_frame::taskbar.progress`에 싣는다. `downloading`은 `normal`과 `received / total`, `checking`·`verifying`은 `indeterminate`, `failed`는 `error`, 나머지는 `none`이다([작업 표시줄](taskbar-design.md), [taskbar.h](../include/luil/app/taskbar.h) 26~31줄).

**키와 교체.** 앱은 `keys`에 키를 여럿 싣는다. 바꿀 때는 새 키를 더한 판을 먼저 내고, 매니페스트를 두 키로 서명하는 동안을 두고, 옛 키를 뺀 판을 낸다. 비밀 키의 보관과 유출 대응은 앱의 몫이다. 옛 키만 믿는 옛 판은 그 키가 새면 막을 길이 없다. 키를 뺀 새 판으로 올라가야 풀린다. 서명 도구는 싣지 않는다. 앱은 openssl이나 쓰는 KMS로 서명한다.

```text
openssl ecparam -name prime256v1 -genkey -noout -out update-key.pem         비밀 키 (저장소 밖에 둔다)
openssl ec -in update-key.pem -pubout -outform DER | openssl base64 -A      update_key::spki_base64
openssl dgst -sha256 -sign update-key.pem -out payload.sig payload.json     서명 항목의 "der"
openssl base64 -A -in payload.json                                          봉투의 "payload"
```

**화면이 없다.** 그리는 것이 없어 접근성·배율·테마·고대비·움직임 줄이기와 닿지 않는다. 라이브러리가 짓는 사람 글이 없다. 단계는 enum이고 `error`는 영문 진단이라 `default_text`에 더하지 않는다. `notes`는 서버의 글이라 앱이 그대로 보인다.

## 결정

- 확정(2026-11-03): N4-apply 길 b. 새 exe가 도우미로 떠서 옛 앱 종료를 기다려 실행 파일을 바꾸고 다시 띄운다. 근거: [platform_services.h](../include/luil/app/platform_services.h) 42·123줄, [jump_list.cpp](../src/win32/jump_list.cpp) 112줄. 세부는 [적용 문서](auto-update-apply-design.md).
- 확정(2026-11-03): N4-trust 길 a. 매니페스트 ECDSA P-256 서명을 OS 암호 API(Win32 CNG, Android JNI의 `java.security`)로 확인하고 받은 파일의 SHA-256을 맞춘다. 근거: [app_identity.cpp](../src/win32/app_identity.cpp) 27줄, [secret_store_android.cpp](../src/android/secret_store_android.cpp) 82줄.
- 확정(2026-11-03): N4-android 길 b. 확인은 같이 하고 적용은 `open_url`로 스토어 페이지를 연다. 근거: [platform_services.h](../include/luil/app/platform_services.h) 134·144줄.

**1. 매니페스트와 서명을 담는 꼴.** 질문: 서명을 어디에 싣고 무엇을 덮게 하는가.

- 길 a: 한 파일 봉투(`payload`는 매니페스트 바이트의 base64, `signatures`는 키 이름과 서명의 목록). 되는 것: 요청이 하나라 두 파일이 어긋날 틈이 없다. 서명이 바이트 그대로를 덮어 정규화가 없다. 서명 여럿이 키 교체를 덮는다. 대가: 서명 확인 전에 바깥 JSON(작은 고정 꼴, 1 MiB 상한)을 읽는다. payload가 base64라 서버에서 사람이 바로 읽지 못한다.
- 길 b: 매니페스트 JSON과 따로 둔 `.sig`. 되는 것: 확인 전에 아무것도 해석하지 않고 매니페스트를 사람이 읽는다. 대가: 요청이 둘이고 서버가 두 파일을 바꾸는 사이에 받으면 한 번 실패한다. 서명 여럿을 담을 꼴을 따로 정한다.
- 길 c: 매니페스트 JSON 안에 서명 칸을 둔다. 되는 것: 한 파일이고 사람이 읽는다. 대가: 서명할 바이트를 JSON에서 다시 짓는 정규화(JCS 같은 것)를 서명 도구와 두 플랫폼이 똑같이 지켜야 한다. nlohmann의 출력과 도구가 한 글자만 달라도 확인이 실패한다.
- 권장: a. 대가: 확인 전 바깥 JSON 읽기와 사람이 읽기 어려운 payload다. 다른 꼴로 바꾸면 서버 파일과 모든 앱 판이 함께 바뀌어 되돌리기 어렵다. **승인 필요.**

**2. 서명과 공개 키의 바이트 꼴.** 질문: CNG의 `BCryptVerifySignature`는 r||s 64바이트를, Java의 `Signature`는 DER을 받는다고 알려져 있다. 두 플랫폼이 같은 서버 파일을 읽게 누가 무엇을 바꾸는가.

- 길 a: 서명은 raw(r||s)만 받는다. 되는 것: Win32는 그대로 쓰고 Android는 C++이 DER을 짓기만 한다. 밖에서 온 ASN.1을 읽는 코드가 없다. 대가: openssl `dgst -sign`과 여러 KMS가 내는 DER을 앱의 서명 과정이 raw로 바꿔야 한다.
- 길 b: 서명은 DER만 받는다. 되는 것: openssl 출력 그대로다. 대가: Win32가 밖에서 온 DER을 읽는다. raw를 내는 도구는 바꿔야 한다.
- 길 c: 둘 다 받고 칸 이름(`raw`·`der`)으로 가른다. 길이로 짐작하지 않는다(64바이트 DER도 있을 수 있다). Win32는 DER을 엄격히 풀어(SEQUENCE 하나에 최소 꼴 INTEGER 둘, 뒤 바이트 없음, 0 거절) raw로, Android는 raw를 DER로 바꾼다. 되는 것: 어느 도구의 출력이든 그대로 싣는다. 대가: 바꾸기 둘과 test 표 둘이다. 둘 다 core의 순수 함수라 `luil_core_tests`가 잠근다.
- 권장: c. 공개 키는 SPKI DER 하나로 정한다(openssl `ec -pubout`의 출력). Win32는 고정 머리(P-256 OID부터 비압축 점 표식 `04`까지)를 맞춘 뒤 X·Y 64바이트를 쓰고, Android는 그대로 `X509EncodedKeySpec`에 넣는다. 머리 값은 알려져 있으나 실측 전이다. Android의 `SHA256withECDSAinP1363Format`은 minSdk 26에서 있다고 믿을 근거가 없어 쓰지 않는다(추측). 대가: 위의 바꾸기 둘이다. **권장대로 진행.**

**3. 판 비교.** 질문: `version`과 `current_version`을 어떻게 견주는가.

- 길 a: 점으로 나눈 십진 마디 1~4개(각 0~4294967295, 앞자리 0 없음), 빠진 마디는 0이다. `1.4`와 `1.4.0`은 같다. 되는 것: Windows 판 정보의 네 마디와 맞고 규칙이 짧다. 미리 보기 판은 채널이 가른다. 대가: `1.4.0-beta.1` 같은 글을 받지 않는다.
- 길 b: SemVer 2.0(미리 보기 꼬리의 차례, 빌드 꼬리 무시). 되는 것: 흔한 판 글을 그대로 쓴다. 대가: 꼬리 차례 규칙이 길고 채널과 뜻이 겹친다.
- 길 c: 앱이 비교 함수를 준다. 되는 것: 무엇이든 된다. 대가: 되감기 막기가 앱 코드의 정확성에 기댄다.
- 권장: a. b는 `format`을 올려 나중에 더할 수 있다. 대가: 미리 보기 꼬리가 없다. **권장대로 진행.**

**4. 옛 매니페스트의 재생.** 질문: 공격자가 예전에 서명된 매니페스트를 다시 내밀 때 무엇을 막는가.

- 길 a: 판만 본다. 되는 것: 상태가 없다. 대가: 지금 판보다 크기만 하면 옛 매니페스트가 지나 알려진 약점이 있는 중간 판으로 올라갈 수 있다.
- 길 b: 매니페스트에 `sequence`(서명된 정수)를 두고 지나간 가장 큰 값을 받기 폴더의 `update-state.json`에 `replace_file_atomically`로 적는다. 작은 값은 `replayed`다. 되는 것: 한 번이라도 새 매니페스트를 본 PC는 옛 것으로 내려가지 않는다. 대가: 처음 보는 PC는 막지 못한다. 폴더를 지우면 기록도 사라진다.
- 길 c: b에 만료 시각 `expires`를 더한다. 되는 것: 처음 보는 PC도 오래된 매니페스트를 거른다. 대가: PC 시계가 틀리면 모든 확인이 실패한다. 서버가 만료 전에 늘 다시 서명해야 한다.
- 권장: b. 대가: 처음 보는 PC의 재생과 폴더 삭제다. 네트워크를 쥔 공격자는 어느 길에서든 갱신을 막을 수 있다. **권장대로 진행.**

**5. 큰 파일을 받는 길.** 질문: 메모리 벡터 하나에 64 MiB 상한인 `http_client`로 수백 MiB를 어떻게 받는가.

- 길 a: 지금 `http_client`로 `Range` 덩이를 차례로 받는다(규칙). 되는 것: http 공개 형식과 두 백엔드를 고치지 않는다. 메모리는 덩이 하나다. 진행이 덩이마다 서고 이어받기가 파일 길이만으로 된다. 대가: `Range`를 받지 않는 서버의 큰 파일은 `range_unsupported`다. 덩이마다 요청 하나가 더 든다.
- 길 b: 요청 하나에 `max_body_bytes`를 asset 크기로 올린다. 되는 것: 가장 작다. 대가: 파일 전체가 메모리에 선다. 진행 표시는 `indeterminate`뿐이고 끊기면 처음부터다.
- 길 c: `http_request`에 파일로 받기와 진행 알림을 더한다. 되는 것: 다른 앱도 큰 파일을 받는다. 대가: `http_message.h`·WinHTTP·Android 백엔드와 [HTTP client](http-client-design.md) 계약을 깊게 고친다. 크기가 L에서 XL로 오른다.
- 권장: a. c는 다른 소비자가 서면 따로 노드로 낸다. 대가: `Range` 없는 서버다. 정적 파일 서버와 CDN은 대개 받는다고 알려져 있다. **승인 필요.**

**6. 흐름을 누가 쥐는가.** 질문: 라이브러리가 주는 것이 상태 기계인가, 조각인가.

- 길 a: `updater` 객체가 자기 `http_client`로 확인·받기·적용을 몰고 `report`로 단계를 알린다. 앱은 부를 때만 정한다. 되는 것: 서명 확인을 건너뛰는 길이 공개 API에 없다. 되감기·이어받기·잠금·결과 파일이 한곳에서 맞물린다. 대가: thread 하나와 공개 타입 여럿이 선다. 앱이 이미 가진 client와 proxy 설정을 함께 쓰지 못한다.
- 길 b: 순수 조각(매니페스트 확인, 파일 해시, 적용 띄우기)만 내고 받기는 앱이 자기 `http_client`로 한다. 되는 것: 운반을 앱이 고른다. 대가: 차례(서명 확인 뒤 읽기, 해시 뒤 적용)를 앱이 지켜야 해 하나를 빠뜨리면 그대로 구멍이다. 받기·이어받기는 앱마다 다시 짠다.
- 길 c: `ui_frame`에 바라는 단계를 싣고 host가 몬다. 되는 것: 다른 frame 칸과 모양이 같다. 대가: 갱신은 상태가 아니라 사건이다. `app_host.h`를 고치고 frame이 오지 않는 동안 멈춘다.
- 권장: a. b의 조각은 비공개 core 헤더로 두고 소비자가 서면 연다. 대가: thread 하나와 따로 선 client다. **승인 필요.**

## 플랫폼

### Win32

- `src/win32/update_crypto_win32.cpp`가 CNG를 부른다. SHA-256은 `BCRYPT_SHA256_ALGORITHM` 해시 개체에 덩이마다 `BCryptHashData`다. 서명은 X·Y 앞에 `BCRYPT_ECCKEY_BLOB { BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32 }`를 붙여 `BCryptImportKeyPair(BCRYPT_ECCPUBLIC_BLOB)`로 들이고, payload의 SHA-256 32바이트와 raw 64바이트로 `BCryptVerifySignature`를 부른다. `STATUS_INVALID_SIGNATURE`는 "맞지 않는다", 그 밖의 실패는 진단 글이다. 상수와 blob 꼴은 알려져 있으나 이 저장소에서 실측 전이다.
- 링크 목록에 `bcrypt` 한 줄을 더한다(src/CMakeLists.txt의 `advapi32` 옆). `app_identity.cpp`의 CryptoAPI는 그대로 둔다. HTTP는 WinHTTP 백엔드의 기존 계약 그대로이고 `decompress = false`는 자동 풀기를 켜지 않는다.
- `win32_window.cpp`는 고치지 않는다. 적용도 고치지 않는다(적용 문서).

### Android

- 확인과 서명 확인은 Win32와 같다. HTTP는 JNI의 `HttpURLConnection` 백엔드다. `decompress = false`면 `Accept-Encoding: identity`를 보낸다.
- `src/android/update_crypto_android.cpp`가 `KeyFactory.getInstance("EC").generatePublic(new X509EncodedKeySpec(spki))`와 `Signature.getInstance("SHA256withECDSA")`의 `initVerify`·`update`·`verify`를 JNI로 부른다. raw 서명은 C++이 DER로 감싼다. 모두 framework 클래스 호출이라 Java 소스가 없다([Java 0줄 방침](android-port-plan.md) 576줄. 비밀 저장소가 같은 길로 `java.security.KeyStore`를 부른다, secret_store_android.cpp 82~84줄). `verify()`가 참일 때만 지난다. 서명이 맞지 않으면 예외 없이 거짓을 돌려주므로 거짓과 `SignatureException`(DER 꼴이 깨졌거나 상태가 틀림)을 함께 `signature_invalid`의 "맞지 않는다"로 본다. `InvalidKeySpecException`(`generatePublic`)과 `InvalidKeyException`(`initVerify`)은 키를 들이지 못한 것이라 Win32의 그 밖의 실패처럼 진단 글이다. JNI 실패 검사는 체인 맨 앞이다(`luil_jni_order`). 부르는 thread는 JVM에 붙은 client thread다.
- 받기가 없다. `download`는 거짓이고 `unsupported`다. SHA-256도 쓰지 않는다. 적용은 적용 문서의 `open_url`이다.

## 다른 노드와의 약속

- **적용 문서.** 같은 노드다. 단계 `applying`·`handed_off`, 실패 `not_writable`·`launch_failed`, 상태 파일의 건너뛴 판, 결과 파일과 `last_apply`의 규칙은 그쪽이 정한다. [단일 인스턴스](single-instance-args-design.md) 185줄은 그쪽이 닫는다.
- **HTTP client.** 고치지 않는다(결정 5 a). `Range` 헤더, `decompress`, `assume_kind`, 요청별 `max_body_bytes`, `final_url`을 그대로 쓴다. 결정 5가 c로 바뀌면 `http_message.h`와 두 백엔드를 이 노드가 고친다(줄 4 파일).
- **줄 2 markup.** core의 nlohmann 링크는 style_sheet(G5)가 세웠다. 이 노드는 core 소스 묶음에 파일을 더할 뿐이고 링크 줄은 고치지 않는다. 같은 `src/CMakeLists.txt`의 다른 묶음이라 병합이 기계적이다.
- **공유 꼬리 파일.** `app_host.h`(ui_frame)·`app_delegate.h`·`ui_platform.h`·`ui_events.h`·`accessibility.h`·`uia_provider.cpp`를 고치지 않는다. 작업 표시줄(R1)의 `taskbar.h`도 고치지 않고 앱이 `update_status`를 진행률로 옮긴다(규칙). 새 kind가 없어 `ui_element_id.h`, `inspect_names.cpp`, `tests/ui_element_tests.cpp`의 `last[]`, `default_text`를 고치지 않는다. `luil.h`에 include 한 줄, `src/CMakeLists.txt`에 core·Win32·Android 묶음의 파일과 `bcrypt`, `tests/CMakeLists.txt`에 test 파일을 끝에 더한다.
- **줄 4가 내는 한 줄.** 이 노드는 `win32_window.cpp`를 고치지 않지만 이 라운드에서 그 파일은 줄 4 몫이다. 다른 줄이 한두 줄이 필요하면 통합 때 줄 4가 따로 커밋으로 낸다(R2 inspector 선례, single-instance-args-design.md 186줄).
- **데모 공용 파일.** `examples/demo_main.cpp`는 여러 줄이 고친다(데모 페이지 바꿈은 page_anim 자리다). 이 노드는 `wWinMain` 첫머리 한 갈래(적용 문서), 작업 표시줄 진행률 한 줄(790줄 `frame->taskbar` 다음), 메시지 길을 고친다. 라이브러리 커밋과 데모 연결 커밋을 나누고 데모 연결은 통합 때 직렬로 쌓는다. `docs/README.md` 색인도 통합 때 붙인다.

## 확인

**이 PC 흔적.** 이 문서의 test와 데모가 남기는 것이다. 적용 test의 흔적은 적용 문서에 있다.

| 무엇 | 자리 | 지우는 법 |
| --- | --- | --- |
| port | `loopback_http_server`의 127.0.0.1 임의 port, test·데모 프로세스 안 | 프로세스가 끝나면 닫힌다 |
| 임시 폴더 | `%TEMP%\luil-update-<pid>\`(받기 폴더, `.part`·`.exe`·`update-state.json`·`update.lock`) | test가 끝에 지운다. 끊긴 test는 `%TEMP%\luil-update-*`를 지운다 |
| 프로세스·레지스트리 | 없다. 이 문서의 test는 프로세스를 띄우지 않고 updater는 레지스트리를 쓰지 않는다 | |
| 데모 | `%LOCALAPPDATA%\luil-demo\update\` | 데모 구역의 `받은 파일 지우기` 단추, 또는 그 폴더를 지운다 |
| Android 기기 | `luil_net_tests`의 `/data/local/tmp/luil`(기존 `adb_run`)과 기기 안 되돌이 port | 기존 test와 같다 |

시험 서명 키는 test 고정 자료다. 공개 키와 미리 서명한 봉투는 `tests/update_fixtures.h`·`.cpp`(test와 데모가 같이 쓴다), 비밀 키는 `tests/update_test_key.h`에만 두고 `luil_tests`의 시험 서명(`BCryptSignHash`)만 들인다. `src/`·`include/`에는 비밀 키도 서명 함수도 없다.

- `tests/update_manifest_tests.cpp`(새 파일, core, `luil_core_tests`): base64(패딩 없음·공백·다른 알파벳 거절), 판 읽기와 비교 표(`1.4`=`1.4.0`, 앞자리 0, 다섯 마디, 넘침, 빈 글), 봉투와 payload의 칸·꼴, 모르는 칸 넘김, 서명 항목 고르기(모르는 키 넘김, 9개 거절), `app`·`channel` 어긋남, `sequence`, 플랫폼 칸 고르기, asset 이름 풀기(절대 주소, 이름 하나, `/`·`:` 든 이름 거절, 되돌이가 아닌 http 거절), DER 풀기(높은 비트의 0 앞붙임, 최소 꼴이 아님, 뒤 바이트, 0, 33바이트 초과)와 raw에서 DER 짓기의 왕복, SPKI 머리(다른 곡선 OID, 압축 점, 길이) 거절, 상태 파일 왕복.
- `tests/update_crypto_tests.cpp`(새 파일, `luil_tests`): SHA-256 알려진 값(빈 글, `abc`, 백만 개 `a`)과 덩이로 나눈 증분이 한 번과 같은지, 고정 키의 서명 확인(raw와 DER 맞음, payload 한 바이트 바꿈, 서명 한 바이트 바꿈, 다른 키, r 또는 s가 0).
- `tests/updater_tests.cpp`(새 파일, `luil_tests`): 되돌이 서버(`loopback_http_server`, 처리기가 `Range`에 206으로 답한다)로 끝까지 돈다. 같은 판 `up_to_date`, 낮은 판 `up_to_date`, 새 판 `available`과 `update_offer`, 깨진 서명·모르는 키 `signature_invalid`, 다른 채널 `manifest_mismatch`, 작은 `sequence` `replayed`, 플랫폼 칸 없음, 크기 초과, 덩이 받기의 진행이 늘기만 하고 마지막이 `total`인지, `Range`를 무시하는 서버의 큰 파일 `range_unsupported`, 바꾼 바이트 `hash_mismatch`와 `.part` 삭제, 덩이 중간에 끊긴 뒤 다음 `download`의 첫 `Range` 시작이 파일 길이인지, 받아 둔 같은 파일이면 요청 없이 `ready`, 다른 `file_lock`이 쥐면 `busy`, `cancel`의 `cancelled`, `stop` 뒤 `report`가 없음, `report`가 부른 thread가 아님.
- Android: `luil_net_tests`(JVM 안의 공유 라이브러리, tests/CMakeLists.txt 313줄)에 `updater.cpp`·`update_crypto_android.cpp`·`update_apply_android.cpp`와 그것이 부르는 `platform_services_android.cpp`(`open_url`)를 더해(`update_manifest.cpp`는 이미 링크하는 `luil_core`에 있다) 같은 서명 벡터(미리 서명한 것. payload 한 바이트를 바꾼 봉투는 `verify()`의 거짓 갈래로 `signature_invalid`다)와 되돌이 확인(`up_to_date`·`available`·`signature_invalid`), `download`의 `unsupported`를 기기에서 돈다.
- 데모 `examples/demo/network_page.cpp`에 "자동 업데이트" 구역을 더한다. 기존 되돌이 서버가 미리 서명한 봉투와 정해진 3 MiB asset을 낸다. 단추 `업데이트 확인`·`내려받기`·`취소`·`받은 파일 지우기`와 확인란 `변조된 파일 내기`·`서명 깨기`·`Range 무시`를 둔다. label `업데이트 상태: 대기`(`확인 중`·`최신 판`·`새 판 1.0.1 · 3.0 MiB`·`받는 중 1.5/3.0 MiB`·`확인 중`·`준비됨`·`실패 (hash_mismatch)`)이 UIA로 읽힌다. 받는 동안 주 창 작업 표시줄에 진행률을 싣는다. 적용 단추는 두지 않는다(적용 문서).
- 사람 확인: 실제 배포 서버(CDN)에서 확인과 받기, 프록시·방화벽 환경, 느린 네트워크에서 큰 파일의 진행 표시와 끊긴 뒤 이어받기, 작업 표시줄 진행률의 모양.

## 범위 밖

- 서명 도구와 키 보관(앱의 몫, 위 openssl 예), 여러 서명이 함께 맞아야 하는 문턱 서명, 만료 시각(결정 4 c).
- SemVer 꼬리(결정 3 b), 차등 갱신(바뀐 바이트만 받기), 앱이 고르는 운반·인증 헤더(결정 6 b).
- `http_client`의 파일로 받기와 진행 알림(결정 5 c).
- 라이브러리가 짓는 갱신 화면(묻는 대화 상자, 알림 문구)과 확인 주기 타이머. 앱이 `update_status`와 자기 타이머로 짓는다.
