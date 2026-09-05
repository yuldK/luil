# Skia 빌드 준비

luil은 Skia 헤더와 정적 라이브러리를 사용한다. 제공된 취득 스크립트로
패키지를 설치하거나, 요구 사항에 맞게 직접 빌드한 Skia를 지정할 수 있다.
CMake configure는 준비된 파일을 검사하고 연결한다.

## 패키지 설치

저장소 루트에서 실행한다.

```powershell
scripts\fetch_skia.ps1 -Configuration Debug,Release
```

기본 설치 위치는 `third_party/skia-prep`이고 아카이브 캐시는
`third_party/skia-prep-archives`다. 이 디렉터리들은 Git으로 추적하지 않는다.
필요한 구성만 선택하려면 `-Configuration Release` 또는 `-Configuration Debug`를 사용한다.
다중 구성 preset이 Debug와 Release를 모두 검사한다면 두 패키지를 준비한다.

스크립트는 [패키지 설정](../third_party/skia-prep.json)의 URL·버전·SHA-256을 사용한다.
체크섬이 다르거나 설치 manifest의 Skia 버전이 맞지 않으면 설치를 중단한다.
같은 패키지가 설치되어 있으면 재설치를 생략하며 `-Force`로 다시 설치할 수 있다.

| 옵션 | 용도 |
| --- | --- |
| `-Configuration` | 설치할 Debug·Release 구성 |
| `-Destination` | 패키지를 풀 디렉터리 |
| `-ArchiveDirectory` | 아카이브 캐시 디렉터리 |
| `-PinFile` | 버전과 다운로드 정보를 담은 설정 파일 |
| `-Force` | 설치 상태와 관계없이 패키지 재설치 |

네트워크를 사용할 수 없는 환경에서는 설정 파일에 지정된 아카이브를
캐시 디렉터리에 준비한다. 캐시 파일에도 같은 체크섬 검사가 적용된다.

## 패키지 내용

- Skia 공개 헤더와 공개 헤더가 참조하는 보조 헤더
- 구성별 정적 라이브러리와 `args.gn`
- 라이선스·제3자 고지
- 버전과 파일 체크섬을 담은 `VERSION.json`

패키지의 구성별 라이브러리 위치는 기본 CMake 설정이 선택한다.
위치를 직접 지정할 때는 `LUIL_SKIA_BUILD_DEBUG`와 `LUIL_SKIA_BUILD_RELEASE`를 사용한다.
소비자 프로젝트에서 사용할 라이브러리 디렉터리를 지정한다.

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

## 구성 검사

[Skia 의존성 모듈](../cmake/dependencies/skia.cmake)은 빌드 트리와 설치본에서
같은 검사를 수행한다.

- 선택한 구성에 필요한 정적 라이브러리가 모두 존재해야 한다.
- Debug는 정적 Debug CRT, 그 밖의 구성은 정적 CRT와 호환되어야 한다.
- luil이 요구하는 렌더러와 이미지 코덱의 GN 설정이 켜져 있어야 한다.

요구 라이브러리 목록은 `LUIL_SKIA_COMPONENTS`, GN 설정은
`LUIL_SKIA_REQUIRED_ARGUMENTS`가 정의한다. 여기에는 Direct3D,
JPEG·WebP·GIF·Rust PNG 디코딩 설정이 포함된다.
현재 PNG 경로는 APNG를 읽을 수 있는 Rust 코덱을 사용한다.
이미지 API가 지원하는 형식과 제한은 [이미지 디코딩](image-decode-design.md)을 본다.

검사는 configure 대상 구성에 적용된다. Release만 구성하는 소비자에게는
Debug 산출물이 필요하지 않다. Visual Studio 같은 다중 구성 generator에서는
`CMAKE_CONFIGURATION_TYPES`가 검사할 구성을 정한다.

## 라이선스와 배포

`cmake/generate_notices.cmake`는 Skia와 포함된 구성 요소의 고지를 읽어
실행 파일 리소스에 넣을 문서를 생성한다. 패키지의 `NOTICE.md`는
정적으로 포함된 구성 요소의 고지를 제공한다.

직접 빌드한 소스 트리를 지정하면 고지를 개별 의존성 디렉터리에서 읽을 수 있다.
이 경우 Rust 구성 요소의 고지가 완전하지 않을 수 있으며 configure가 경고한다.
배포에는 포함된 모든 구성 요소의 고지를 갖춘 패키지를 사용한다.

## 의존성 버전 변경

새 패키지로 갱신할 때는 설정 파일의 버전·다운로드 주소·체크섬을 함께 갱신한다.
요구 라이브러리 목록, GN 설정, 공개 헤더가 사용하는 Skia API의 호환성을 확인한다.
Debug·Release 빌드, 이미지 디코딩 테스트, 렌더러 스모크와 설치본 소비자 테스트로 검증한다.
