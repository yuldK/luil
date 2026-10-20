# Skia 빌드 준비

luil은 Skia 헤더와 정적 라이브러리를 사용한다. 제공된 취득 스크립트로
패키지를 설치하거나, 요구 사항에 맞게 직접 빌드한 Skia를 지정할 수 있다.
CMake configure는 준비된 파일을 검사하고 연결한다.

현재 핀은 두 대상 모두 **Skia 157(`b94213a11b3a`), 패키지 r1**이다.

## 패키지 설치

저장소 루트에서 실행한다.

```powershell
scripts\fetch_skia.ps1 -Configuration Debug,Release
scripts\fetch_skia.ps1 -Target android-arm64 -Configuration Debug,Release
```

패키지는 대상마다 핀 하나와 설치 자리 하나를 갖는다. `-Target`의 기본값은 `win-x64`다.

| 대상 | 핀 | 설치 자리 |
| --- | --- | --- |
| `win-x64` | [`third_party/skia-prep.json`](../third_party/skia-prep.json) | `third_party/skia-prep` |
| `android-arm64` | [`third_party/skia-prep-android-arm64.json`](../third_party/skia-prep-android-arm64.json) | `third_party/skia-prep-android-arm64` |

핀의 `target`이 고른 대상과 다르면 받거나 지우기 전에 멈춘다.
기본 설치 위치(win-x64)는 `third_party/skia-prep`이고 아카이브 캐시는
`third_party/skia-prep-archives`다. 이 디렉터리들은 Git으로 추적하지 않는다.
필요한 구성만 선택하려면 `-Configuration Release` 또는 `-Configuration Debug`를 사용한다.
다중 구성 preset이 Debug와 Release를 모두 검사한다면 두 패키지를 준비한다.

스크립트는 [패키지 설정](../third_party/skia-prep.json)의 URL·버전·SHA-256을 사용한다.
체크섬이 다르거나 아카이브가 설정과 다른 패키지이면 설치를 중단한다.

같은 패키지가 이미 설치되어 있으면 재설치를 생략한다. `-Force`로 다시 설치한다.
같은 패키지인지는 설치본의 `VERSION.json`이 스스로 적어 둔 값으로 판정한다.

실제로 설치한 파일의 수정 시각은 설치 시각으로 갱신한다. 재현 가능한 아카이브는
파일 시각이 패키지 빌드 시각으로 고정되어 있어, 소비자의 기존 산출물보다 오래될 수
있다. 그대로 두면 Ninja가 갱신된 헤더·라이브러리로 다시 빌드하지 않는다. 설치를
생략할 때는 수정 시각도 유지하여 불필요한 재빌드를 막는다.

| 보는 것 | 무엇을 가르는가 |
| --- | --- |
| Skia commit·패키지 판번·PNG 코덱·대상·도구사슬 | 어느 패키지인가 |
| 구성별 아카이브 SHA-256 | 그 구성을 어느 자산이 설치했는가 |
| 구성별 `args.gn` 체크섬과 라이브러리 크기 | 설치본이 그대로 남아 있는가 |

Skia commit만으로는 판정하지 않는다. **같은 소스 commit을 다시 패키징한 것이 있기
때문이다.** 도구사슬을 바꾼 r2가 그것이며, commit만 보면 r1이 설치된 기계에서
재설치가 생략되어 낡은 라이브러리가 남는다. 패키지 판번이 그 둘을 가른다.

아카이브는 설치본을 지우기 전에 모두 확보한다. 받지 못하면 이미 설치된 패키지가
그대로 남는다.

| 옵션 | 용도 |
| --- | --- |
| `-Configuration` | 설치할 Debug·Release 구성 |
| `-Target` | 패키지 대상 (`win-x64` 기본, `android-arm64`) |
| `-Destination` | 패키지를 풀 디렉터리 |
| `-ArchiveDirectory` | 아카이브 캐시 디렉터리 |
| `-PinFile` | 버전과 다운로드 정보를 담은 설정 파일 |
| `-Force` | 설치 상태와 관계없이 패키지 재설치 |
| `-Offline` | 네트워크를 시도하지 않음. 아카이브가 없거나 체크섬이 다르면 안내만 내고 실패 |
| `-Symbols` | Release 구성을 심볼 자산(핀의 `ReleaseSymbols`)으로 받음. win-x64만 있다 |

`-Symbols`의 자산은 같은 코드를 `/Z7`로 세운 Release 아카이브다. 앱을 `/DEBUG`로
링크하면 Skia의 심볼이 앱의 PDB에 들어가고, `/DEBUG` 없이 링크한 실행 파일은 기본
자산과 같다 (skia-prep `docs/skia-build.md` 6.7). 설치 자리와 구성 이름은 같고 아카이브만
달라, 켜고 끄면 설치 생략 판정이 어긋나 다시 설치한다. 현재 Windows Release 자산은 약
22 MB, 심볼 자산은 약 89 MB다.

네트워크를 사용할 수 없는 환경에서는 설정 파일에 지정된 아카이브를
캐시 디렉터리에 준비한다. 캐시 파일에도 같은 체크섬 검사가 적용된다.
다운로드 시도 자체가 허용되지 않는 환경에서는 `-Offline`을 함께 준다.
`fetch_webview2.ps1`도 같은 `-ArchiveDirectory`·`-Offline` 스위치를 받는다.

## 패키지 내용

- Skia 공개 헤더와 공개 헤더가 참조하는 보조 헤더
- 구성별 정적 라이브러리와 `args.gn`
- 구성별 `toolchain.json`
- 라이선스·제3자 고지 (`NOTICE.md`)와 기능마다 실을 고지를 적은 `NOTICE.json`
- 판번·도구사슬·구성별 체크섬을 담은 `VERSION.json`
- 기능별 아카이브와 그 공개 헤더(`modules/*/include/`) — 아래 [기능별 아카이브](#기능별-아카이브)

`args.gn`은 어떤 기능으로 생성했는지를 적고, `toolchain.json`은 무엇이 그 라이브러리를
컴파일했는지를 적는다. 둘은 다른 사실이라 파일도 따로다. `args.gn`은 `gn gen`에 준 값이며
컴파일러의 CRT 지시문은 도구사슬이 달라도 같은 것이 나오므로, 컴파일러는 패키지를
만들 때 기록해 두는 수밖에 없다.

```json
{
  "toolchain": "clang", "compiler": "clang-cl",
  "compiler_version": "clang version 19.1.5", "linker": "lld-link",
  "msvc_version": "14.44.35207", "windows_sdk": "10.0.26100.0",
  "configuration": "Release", "png_codec": "rust"
}
```

패키지의 구성별 라이브러리 위치는 기본 CMake 설정이 선택한다.
위치를 직접 지정할 때는 `LUIL_SKIA_BUILD_DEBUG`와 `LUIL_SKIA_BUILD_RELEASE`를 사용한다.
소비자 프로젝트에서 사용할 라이브러리 디렉터리를 지정한다.

## 기능별 아카이브

Skia 152의 win-x64 r3·android-arm64 r2부터 SVG·텍스트·Lottie·AVIF가 별도 아카이브로
제공된다. 157 r1은 ICU가 없는 플랫폼의 텍스트 fallback을 더한다 (skia-prep
`docs/skia-build.md` 6장). luil은 기능마다 옵션을 두고 **모두 기본은 끈다**.

| 옵션 | 패키지 기능 | 아카이브 (win-x64) | 요구 인자 |
| --- | --- | --- | --- |
| `LUIL_ENABLE_SVG` | `svg` | `svg` `skshaper` `skresources` | `skia_enable_svg=true` |
| `LUIL_ENABLE_TEXT_SHAPING` | `text` | `skparagraph` `skshaper` `skunicode_icu` `skunicode_core` `harfbuzz` | `skia_use_harfbuzz=true` `skia_use_runtime_icu=true` |
| `LUIL_ENABLE_TEXT_SHAPING_FALLBACK` | `text_fallback` | `skunicode_libgrapheme` | `skia_use_libgrapheme=true`, 텍스트 옵션도 켜야 한다 |
| `LUIL_ENABLE_LOTTIE` | `skottie` | `skottie` `sksg` `jsonreader` `skresources` `skshaper` | `skia_enable_skottie=true` |
| `LUIL_ENABLE_AVIF` | `avif` | `avif_decoder` `libavif` `libgav1` `libyuv` | `skia_prep_avif_archive=true` |

- 켠 기능의 아카이브를 `LUIL_SKIA_COMPONENTS`에 더하고 요구 인자를
  `LUIL_SKIA_REQUIRED_ARGUMENTS`에 더한다. 함께 쓰는 `skshaper`·`skresources`는 한 번만
  넣는다. 자리는 157 r1에서 실측한 순서인 rust 아카이브 둘 앞이고, Windows는 순서를
  보지 않는다.
- 기능별 아카이브가 없는 앞 판번에서 기능을 켜면 configure가 그 아카이브가 없다고
  멈춘다. 핀을 올리고 다시 받는다.
- 텍스트를 켜면 Skia 타깃이 skshaper·skunicode의 소비자 정의 다섯
  (`SK_SHAPER_PRIMITIVE_AVAILABLE` 등)을 나른다. `SK_DISABLE_LEGACY_SKSHAPER_FUNCTIONS`는
  주지 않는다 — SkShaper의 vtable이 패키지와 어긋난다. fallback을 켜면 여기에
  `SK_UNICODE_LIBGRAPHEME_IMPLEMENTATION`과 해당 아카이브·고지가 더해진다.
- 시스템 라이브러리는 새로 들지 않는다. 텍스트의 ICU는 실행 시점에 플랫폼의 것을 연다.
- 플랫폼 ICU를 열지 못하면 fallback을 켠 구성은 libgrapheme과 컴파일된 ICU BiDi 조각을
  쓴다. fallback을 끈 구성은 primitive 공장으로 내려간다. 테스트·예제 preset은
  fallback도 켜서 검증한다.
- AVIF는 Skia의 기본 디코더 목록 밖이라 luil이 처음 디코드 때 등록한다
  ([이미지 디코딩](image-decode-design.md)).

PNG·JPEG 인코더는 기능별 아카이브가 아니라 core에 있다 (r2까지는 win-x64의 `skia.lib`에
rust PNG 인코더가 빠져 있었다). 부르는 앱에만 링크된다 — [이미지 인코딩](image-encode-design.md).

각 기능이 무엇을 하는지는 [SVG와 Lottie](vector-image-design.md)와
[이미지 디코딩](image-decode-design.md)에 있다.

## 도구사슬·ABI·CRT 계약

**Skia 자신은 clang-cl로 컴파일한다. luil과 소비자는 그대로 MSVC다.**

이유는 CPU 래스터 파이프라인의 처리 폭이다. `SkRasterPipeline`의 벡터 형이 clang과
gcc의 확장이라, 그 밖의 컴파일러로 세운 Skia는 스칼라 경로로 떨어져 한 번에 픽셀
하나를 처리한다. 링크는 성립하고 그림도 맞으므로 조용히 느려지기만 한다.
4000x7000 이미지의 cubic 축소가 799.8 ms에서 13.6 ms로 줄었다.
실측과 재는 방법은 skia-prep의 `docs/skia-build.md` 5.4에 있다.

**소비자에게 clang 설치가 필요하지 않다.** 패키지는 이미 컴파일된 라이브러리이며,
configure는 패키지가 적어 둔 파일을 읽을 뿐 컴파일러를 찾지도 부르지도 않는다.
소비자 프로젝트의 컴파일러는 MSVC 그대로다.

함께 지키는 것이 둘 더 있다.

| 계약 | 값 | 어긋나면 |
| --- | --- | --- |
| 컴파일러 | `clang-cl` | 링크는 되고 CPU 래스터가 수십 배 느리다 |
| ABI | `is_trivial_abi = false` | `sk_sp`의 호출 규약이 갈려 런타임에 깨진다 |
| CRT | Debug `/MTd`, 그 밖 `/MT` | 링커의 `/FAILIFMISMATCH`가 원인 없이 실패한다 |

`is_trivial_abi`가 참이면 clang에서만 `sk_sp` 같은 형이 `[[clang::trivial_abi]]`를 달아
전달 방식이 바뀐다. 같은 헤더를 MSVC로 컴파일하는 luil과 소비자에게는 그 속성이 없어
같은 형이 서로 다른 ABI가 되고, 링크가 성립한 채 런타임에 깨진다. MSVC로 세우던
동안에는 값이 무엇이든 무해했으므로 계약이 아니었다.

## Android 패키지와 링크 계약

`android-arm64` 패키지는 NDK r27d의 clang으로 세운 정적 아카이브 15개다. GPU는 Vulkan(Ganesh)이고 글꼴은 FreeType·expat으로 읽으며 코덱은 Windows와 같다. configure는 Windows와 같은 방식으로 `args.gn`과 `toolchain.json`을 읽되, 대상에 맞는 값을 본다.

| 검사 | win-x64 | android-arm64 |
| --- | --- | --- |
| 필수 인자 | Direct3D, 코덱 | Vulkan, FreeType, expat, 코덱 |
| 컴파일러 (`toolchain.json`의 `compiler`) | `clang-cl` | `clang` |
| ABI | `is_trivial_abi = false` | 같다 |
| 런타임 | Debug `/MTd`, 그 밖 `/MT` | `args.gn`의 `ndk_api`가 구성의 API 수준 이하 |

소비자 링크 계약은 skia-prep `docs/skia-build.md` 8.4·8.5를 따르고, [Skia 의존성 모듈](../cmake/dependencies/skia.cmake)이 그대로 만든다.

- 157 r1은 rust 아카이브에 겹쳐 들었던 Skia 오브젝트를 패키징할 때 걷는다. `--allow-multiple-definition`은 사용하지 않으며 중복 정의가 생기면 링크가 실패한다. 아카이브끼리 서로 부르는 기호는 `$<LINK_GROUP:RESCAN,...>`의 재검색 묶음으로 푼다.
- `-landroid -llog`를 링크한다. 그 밖의 시스템 라이브러리는 libc·libm·libdl뿐이다.
- 최저 API 수준은 26이다 (8.4). C++ 런타임은 NDK의 `c++_static`이다. skia-prep의 기기 검증(`tools/android_probe.cpp`)이 그 조합이었다.
- FreeType은 FTL로 고른다. 앱 문서에 FreeType을 밝힌다 (8.5).
- Vulkan 메모리 할당기는 소비자가 넘긴다 (8.5). 만드는 함수 `skgpu::VulkanMemoryAllocators::Make`가 Skia 내부 헤더에만 있어, luil은 같은 서명을 비공개 헤더 [`vulkan_memory_allocator.h`](../src/android/vulkan_memory_allocator.h)에 선언해 쓴다. 내부 API이므로 Skia milestone을 올릴 때마다 두 구성의 `libskia.a`에 그 기호가 있는지 다시 확인한다 (M157 r1의 Debug·Release에서 확인했다).

## 직접 빌드한 Skia 사용

다음 캐시 변수를 configure 시점에 지정한다.

| 변수 | 값 |
| --- | --- |
| `LUIL_SKIA_ROOT` | Skia 헤더와 고지가 있는 루트 디렉터리 |
| `LUIL_SKIA_BUILD_DEBUG` | Debug 라이브러리와 `args.gn` 디렉터리 |
| `LUIL_SKIA_BUILD_RELEASE` | Release 라이브러리와 `args.gn` 디렉터리 |

개인 설정은 Git에서 제외되는 `CMakeUserPresets.json`에 둘 수 있다.
설치 패키지를 소비하는 프로젝트에서도 같은 변수를 사용한다.
설치본의 기본 경로가 유효하지 않은 환경에서는 이 값을 소비자가 설정한다.

### 직접 빌드한 Skia가 갖추어야 하는 것

검사는 패키지와 같다. 다음을 모두 갖춘 산출 디렉터리여야 한다.

1. `LUIL_SKIA_COMPONENTS`의 정적 라이브러리 전부. Rust PNG 코덱을 쓰므로 bazel
   산출물(`librust_png_ffi_rs.a`, `libcxx_cc.a`)이 함께 있어야 한다.
2. `args.gn`에 luil이 요구하는 GN 설정. Direct3D와 JPEG·WebP·Wuffs·Rust PNG
   디코더와 PNG·JPEG·WebP 인코더다 (`LUIL_SKIA_REQUIRED_ARGUMENTS`).
3. `args.gn`에 `is_trivial_abi = false`. 값이 없으면 통과하지 않는다. GN 기본값도
   거짓이지만, 적지 않은 것과 정하고 적은 것은 다르다.
4. `args.gn`의 `extra_cflags`에 구성에 맞는 CRT. Debug는 `/MTd`, 그 밖은 `/MT`다.
   한 산출 디렉터리에 둘이 함께 있으면 실패한다.
5. clang-cl로 컴파일한 것. `toolchain.json`이 옆에 있으면 그 `compiler` 값을
   보고, 없으면 `args.gn`의 `clang_win`이 가리키는 것이 있는지를 본다.

skia-prep의 `build_skia.ps1`이 이 다섯을 모두 갖춘 산출 디렉터리를 만든다.
`gn gen`을 직접 부를 때는 `skia_use_partition_alloc = false`도 함께 준다. 기본값이
`is_clang`이라 도구사슬을 clang으로 바꾸는 것만으로 켜지고, 켜지면 Skia가 external을
하나 더 요구한다.

## 구성 검사

[Skia 의존성 모듈](../cmake/dependencies/skia.cmake)은 빌드 트리와 설치본에서
같은 검사를 수행한다.

- 선택한 구성에 필요한 정적 라이브러리가 모두 존재해야 한다.
- luil이 요구하는 렌더러와 이미지 코덱의 GN 설정이 켜져 있어야 한다.
- clang-cl로 컴파일한 것이어야 한다.
- `is_trivial_abi = false`로 세운 것이어야 한다.
- Debug는 `/MTd`, 그 밖의 구성은 `/MT`로 세운 것이어야 한다.

요구 라이브러리 목록은 `LUIL_SKIA_COMPONENTS`, GN 설정은
`LUIL_SKIA_REQUIRED_ARGUMENTS`, 도구사슬은 `LUIL_SKIA_REQUIRED_TOOLCHAIN`이
정의한다. 여기에는 Direct3D, JPEG·WebP·GIF·Rust PNG 디코딩 설정이 포함된다.
현재 PNG 경로는 APNG를 읽을 수 있는 Rust 코덱을 사용한다.
이미지 API가 지원하는 형식과 제한은 [이미지 디코딩](image-decode-design.md)을 본다.

뒤의 셋은 어긋나도 링크가 성립하는 것들이라 configure에서 잡는다. 근거는 패키지가
자기 옆에 적어 둔 `args.gn`과 `toolchain.json`이며, configure는 컴파일러를 찾지도
부르지도 않는다. 계약의 내용은 [도구사슬·ABI·CRT 계약](#도구사슬abicrt-계약)에 있다.

검사는 configure 대상 구성에 적용된다. Release만 구성하는 소비자에게는
Debug 산출물이 필요하지 않다. Visual Studio 같은 다중 구성 generator에서는
`CMAKE_CONFIGURATION_TYPES`가 검사할 구성을 정한다.

## 라이선스와 배포

`cmake/generate_notices.cmake`는 Skia와 포함된 구성 요소의 고지를 읽어
실행 파일 리소스에 넣을 문서를 생성한다. 패키지의 `NOTICE.md`는
정적으로 포함된 구성 요소의 고지를 제공한다.

패키지에 `NOTICE.json`이 있으면 **core와 켠 기능의 고지만** 골라 싣는다. `NOTICE.json`의
`features.<기능>.notices`가 그 기능을 쓸 때 실행 파일에 들어오는 구성 요소의 id를, `notices`가
id마다 `NOTICE.md`의 절 이름을 적는다. 끈 기능의 고지(HarfBuzz·ICU·libavif·libgav1·libyuv,
libgrapheme·Windows의 Expat)는 그 코드가 실행 파일에 없으므로 싣지 않는다. 절이 `NOTICE.md`에 없으면
configure가 멈춘다. `NOTICE.json`이 없는 앞 판번은 전처럼 `NOTICE.md` 전체를 싣는다.

직접 빌드한 소스 트리를 지정하면 고지를 개별 의존성 디렉터리에서 읽을 수 있다.
이 경우 Rust 구성 요소의 고지가 완전하지 않을 수 있으며 configure가 경고한다.
배포에는 포함된 모든 구성 요소의 고지를 갖춘 패키지를 사용한다.

## 의존성 버전 변경

새 패키지는 skia-prep이 만든다. 그 저장소의 `pack_skia.ps1`이 릴리스에 올릴 자산과
함께 `build/release/skia-prep.json`을 내는데, 그것이 이 저장소의
`third_party/skia-prep.json`에 들어갈 값이다.

1. skia-prep에서 자산을 릴리스에 올린다 (그 저장소의 `docs/publishing.md`).
2. `third_party/skia-prep.json`을 갱신한다. 태그·다운로드 주소·패키지 판번·도구사슬과
   자산마다의 파일 이름·크기·SHA-256이다. Skia commit이 그대로여도 패키지를 다시
   만들었으면 판번이 오른다.
3. `scripts\fetch_skia.ps1 -Configuration Debug,Release`를 돌린다. 판번이 올랐으므로
   재설치가 생략되지 않는다. 생략되었다면 핀이 갱신되지 않은 것이다.
4. 요구 라이브러리 목록·GN 설정·도구사슬 계약과 공개 헤더가 쓰는 Skia API의
   호환성을 확인한다.
5. Debug와 Release를 모두 빌드하고 테스트를 돌린다. 이미지 디코딩, 래스터 그리기,
   Direct3D를 포함한 렌더러 스모크, 설치본 소비자 테스트가 관문이다.

```powershell
scripts\fetch_skia.ps1 -Configuration Debug,Release
cmake --preset vs2026-tests
cmake --build --preset vs2026-tests-debug
ctest --preset vs2026-tests-debug
cmake --build --preset vs2026-tests-release
ctest --preset vs2026-tests-release
```

네트워크를 쓰지 않는 환경에서는 자산을 `third_party/skia-prep-archives`에 두고
`-Offline`을 함께 준다. CI의 캐시 키가 핀 파일이므로 핀을 갱신하면 새로 받는다.
