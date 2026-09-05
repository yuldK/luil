# luil

luil은 Windows용 C++20 애플리케이션 프레임워크다.

## 빌드

Windows 11 x64, Visual Studio 2022 또는 2026, CMake 4.2 이상을 사용한다.
아래 명령은 저장소 루트에서 실행한다.

```powershell
scripts\fetch_skia.ps1 -Configuration Debug,Release
cmake --preset vs2026
cmake --build --preset vs2026-release
```

Visual Studio 2022에서는 대응하는 `vs2022` preset을 사용한다.

## 문서

- [문서 안내](docs/README.md)
- [컴포넌트 안내](docs/components.md)
