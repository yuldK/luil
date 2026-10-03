# 렌더링

UI 스레드는 logic이 게시한 `ui_frame`과 input의 `interaction_snapshot`을 표면의 크기·DPI·창 상태와 결합한다. 내부 [`frame_state`](../../src/host/frame_state.h)는 물리 픽셀 크기, `dpi_scale`, backend 정보, 테마·accent·고대비 색, 앱이 실은 스타일 포인터, maximized 상태, interaction, tree와 글꼴 포인터, 웹뷰 영역인 `holes`를 담는다. tree·스타일·글꼴 포인터는 렌더 호출 동안 유효해야 한다.

`frame_palette()`가 스타일·테마·accent에서 팔레트 하나를 만든다. [`draw_frame()`](../../src/host/frame_state.cpp)이 그것과 스타일의 치수로 `draw_context`를 구성하고, 창은 같은 팔레트로 DWM 테두리와 웹뷰의 바닥색을 정한다. tree가 없으면 배경만 그린다. tree가 있으면 element와 초점 테·tooltip·drag 표시를 그린 뒤 `holes`의 물리 픽셀 사각형을 알파 0으로 비운다.

## Backend와 실패 처리

내부 [`skia_renderer`](../../src/host/skia_renderer.h)의 가상 함수는 `backend()`, `resize()`, `render()`다. 제시는 각 backend의 `render()` 안에서 처리한다. 이 interface와 실패 물러섬 정책인 `renderer_host`는 플랫폼을 모른다. 플랫폼은 GPU·CPU 렌더러를 만드는 함수와 오류 글에 쓸 GPU 이름을 `renderer_factories`로 넘긴다. Win32에서 CPU backend는 Skia raster 결과를 GDI로 제시하고, Direct3D backend는 D3D12와 Skia Ganesh로 그린 swap chain을 DirectComposition에 붙인다. 웹뷰가 놓이는 합성 자리(`underlay()`)는 Win32 확장인 [`composition_renderer`](../../src/win32/skia_renderer.h)가 낸다. Android에서 CPU backend는 네이티브 창의 버퍼를 잠가 그 메모리에 그리고, Vulkan backend는 Skia Ganesh로 그린 스왑체인 이미지를 표시한다 ([아래](#android의-vulkan)).

`renderer_host`의 정책은 [`renderer_mode`](../../include/luil/app/renderer_policy.h)로 정한다.

| 모드 | 동작 |
| --- | --- |
| `cpu` | CPU renderer만 생성한다. |
| `direct3d` | Direct3D 생성·resize·render 실패를 호출자에게 반환한다. |
| `gpu` | 이 플랫폼의 GPU 경로다. Windows에서는 `direct3d`와 같고 Android에서는 Vulkan이다. 앱이 플랫폼마다 다른 값을 쓰지 않도록 둔다. |
| `automatic` | GPU(Windows는 Direct3D, Android는 Vulkan)를 우선하며 생성·resize·render 실패 시 CPU로 전환한다. |

CPU 전환도 실패하면 오류를 반환한다. CPU로 전환한 host가 다음 frame에서 자동으로 Direct3D를 다시 시도하지는 않는다. 전환 시 이전 DirectComposition target을 해제해야 GDI 결과가 화면에 드러난다.

## CPU 전용 빌드

`LUIL_ENABLE_DIRECT3D=OFF`로 구성하면 Direct3D 렌더러 대신 "없음"을 돌려주는 stub이 들어간다. 모드별 동작은 다음과 같다.

| 모드 | CPU 전용 빌드의 동작 |
| --- | --- |
| `cpu` | 기본 빌드와 같다. |
| `automatic` | Direct3D 생성 실패와 같은 길로 CPU에 물러선다. |
| `direct3d` | 창 생성이 실패한다. smoke test에서는 종료 코드가 77이다. |

표면은 DirectComposition device도 만들지 않으므로 프로세스에 D3D12·DXGI가 올라오지 않는다. 앱 코드는 바꿀 필요가 없다. 나중에 GPU가 필요해지면 옵션만 다시 켠다.

2026-09-29에 RTX 4080(드라이버 32.0.16.1714)에서 Release `hello`로 측정한 값이다.

| 구성 | 실행 파일 | Private 메모리 |
| --- | --- | --- |
| 기본 빌드, `automatic`(Direct3D) | 8.07MB | 약 161MB |
| 기본 빌드, `cpu` | 8.07MB | 약 7MB |
| CPU 전용 빌드, `automatic` | 5.17MB | 약 7MB |

메모리 차이는 렌더러 모드에서 나고 빌드 옵션에서 나지 않는다. CPU로 그리면 두 빌드 모두 약 7MB다(여러 번 측정해 6.9–7.4MB). 빌드 옵션이 줄이는 것은 실행 파일 크기와 올라오는 DLL(D3D12·DXGI·D3DCompiler)이다.

Direct3D의 상주 메모리는 대부분 드라이버가 장치·큐·스왑체인에 잡는 몫이라 프로세스마다 따로 든다. CPU 렌더러는 FHD 이하에서 한 frame에 수 ms 안쪽이다. 4K에서 계속 움직이는 화면처럼 프레임 예산이 빠듯한 앱에만 Direct3D가 필요하다.

## Android의 Vulkan

Android 앱 host([`android_app.h`](../../include/luil/android/android_app.h))의 기본 모드는 `automatic`이다. `libvulkan.so`를 dlopen하므로 Vulkan이 없는 기기에서도 라이브러리가 열리고 CPU로 물러선다.

**장치와 스왑체인의 수명이 다르다.** 창은 회전이 아닌 일(홈, 화면 끄기, 다른 앱)마다 사라졌다 다시 생긴다. [`vulkan_device`](../../src/android/vulkan_device.h)(인스턴스, 장치, queue, Skia context)는 Activity가 사는 동안 하나로 남고, 창마다 [`vulkan_skia_renderer`](../../src/android/vulkan_skia_renderer.h)가 `VkSurfaceKHR`과 스왑체인만 새로 세운다. 창이 사라지면 Skia가 쥔 GPU 자원을 돌려준다(`freeGpuResources`).

- 스왑체인은 FIFO(수직 동기)이고 이미지 수는 표면의 최소값이다(확인한 기기에서는 셋). 형식은 CPU 경로와 같은 RGBA다.
- 이미지를 받을 때 새 semaphore로 기다리고, 다 그리면 이미지마다 하나인 semaphore에 신호한 뒤 표시한다. 낡은 스왑체인(`VK_ERROR_OUT_OF_DATE_KHR`)은 다시 세운다.
- 아직 한 번도 그리지 않은 이미지가 남아 있는 동안은 semaphore 대신 fence로 받아 CPU에서 기다린다. 새 이미지는 레이아웃이 UNDEFINED이고 Skia가 그 전환 장벽을 TOP_OF_PIPE에서 시작해, semaphore를 기다리는 단계와 이어지지 않기 때문이다(동기화 검증의 `SYNC-HAZARD-WRITE-AFTER-READ`). 스왑체인이 설 때 처음 몇 frame만 해당한다.
- 전변환은 identity다. 회전은 컴포지터가 한다. 회전된 표면에 identity를 쓰면 Android는 표시할 때마다 `VK_SUBOPTIMAL_KHR`을 돌려주므로, 그 까닭이면 다시 세우지 않는다. 확인한 기기에서는 가로 화면의 레이어를 HWC가 `ROT_90`으로 직접 합성해(`DEVICE`) GPU 합성 비용이 없었다. HWC가 회전을 못 하는 기기에서는 GPU 합성으로 내려가므로, 그때는 표면의 `currentTransform`에 맞춰 캔버스를 돌려 그리는 쪽을 다시 본다.
- GPU 대기(스왑체인을 다시 세우거나 버리기 전, 이미지 받기)는 상한이 있다. 빈 제출에 fence를 걸어 [`fence_wait.h`](../../src/host/fence_wait.h)의 예산만큼 쪼개 기다리고, 넘기면 실패로 돌려 물러섬에 맡긴다.
- 장치를 잃거나 그리다 실패하면 CPU로 물러선다. Windows는 표면 하나가 물러서지만 Android는 **Activity 전체**가 물러선다. 창이 다시 생겨도 CPU로 그리고 장치를 놓는다. 물러설 때는 Vulkan 렌더러가 먼저 사라져 창과의 연결을 끊는다. 창은 생산자를 하나만 받으므로 그래야 CPU 렌더러가 버퍼를 잠글 수 있다.

**경로 재현.** 앱이 `apply_debug_properties(config)`를 부르면 시스템 속성으로 모드와 실패 주입을 바꿔 띄울 수 있다. 예제는 부른다.

```powershell
adb shell setprop debug.luil.renderer cpu                      # auto, cpu, gpu
adb shell setprop debug.luil.simulate_gpu_failure 1            # 생성 실패
adb shell setprop debug.luil.simulate_gpu_loss_after_frames 3  # 세 frame 뒤 손실
adb shell setprop debug.luil.renderer "''"                     # 지운다
```

고른 렌더러는 logcat의 `luil` 태그에 `renderer vulkan on Adreno (TM) 730`처럼 남는다. 장치는 `vulkan device created`가 Activity마다 한 번이다.

**검증 레이어.** Khronos [Vulkan-ValidationLayers](https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases) 릴리스의 `android-binaries-<판>.zip`에서 `arm64-v8a/libVkLayer_khronos_validation.so`를 꺼내 `<dir>/arm64-v8a/`에 두고 debug APK에 함께 싼다. debug APK는 디버그 가능 앱이라 시스템 속성으로 레이어를 켤 수 있다. 오류는 logcat의 `VALIDATION` 태그로 나온다. 동기화 검증은 기본으로 꺼져 있으므로 `debug.vulkan.khronos_validation.validate_sync`를 `true`로 함께 켠다. 레이어 1.4.363.0으로 hello·widgets의 회전·홈·물러섬 경로에서 오류가 없고, 남는 것은 identity 전변환을 알리는 성능 경고(`WARNING-Swapchain-PreTransform`)뿐이다.

```powershell
examples\android\gradlew.bat -p examples\android :app:assembleHelloDebug -Pluil.vulkanLayerDirectory=<dir>
adb shell setprop debug.vulkan.layers VK_LAYER_KHRONOS_validation
adb shell setprop debug.vulkan.khronos_validation.validate_sync true
adb shell setprop debug.vulkan.layers "''"                     # 끝나면 지운다
```

**메모리.** 1440x3088 화면의 기기에서 hello의 `dumpsys meminfo` Graphics는 그리는 동안 약 118MB(스왑체인 이미지 셋이 약 53MB), 백그라운드에서 약 24MB다.

## 다시 그리기

[`window_surface`](../../src/win32/window_surface.cpp)는 frame·interaction 게시, 창 크기·DPI·테마 변경, timer를 무효화 요청으로 모으고 `WM_PAINT`에서 그린다. interaction은 표면 id로 걸러 다른 창의 hover·focus·drag가 섞이지 않게 하고, caption의 비클라이언트 hover를 합친다.

`ui_tree::next_update()`는 보이는 element와 tooltip의 가장 이른 갱신 시각을 반환한다. 플랫폼은 표면들의 예고를 모아 timer를 예약한다. 미래 시각은 그때 다시 그리라는 뜻이고, 현재 이하의 시각은 계속 갱신하라는 뜻이며, `nullopt`는 시간에 따른 그림 변경이 없다는 뜻이다. 앱 상태 자체의 시간 변경은 `logic_driver::next_tick()`과 `tick()`으로 처리한다.

## 웹뷰 합성

`webview_element`는 tree 안에서 웹뷰의 자리와 잘림을 제공한다. 렌더 전에 `apply_webviews()`가 배치와 가림 상태를 계산하고 renderer 아래의 composition visual에 페이지를 배치한다. native tree를 모두 그린 뒤 해당 영역만 비우므로 페이지가 드러난다. native 콘텐츠가 웹뷰 위를 가려야 하는 frame은 배치 계획에서 그 영역을 구멍 목록에서 제외한다.

CPU renderer의 `underlay()`는 null이므로 페이지를 합성할 수 없다. 이 경우와 WebView2를 만들 수 없는 경우에는 tree의 placeholder가 남는다. Direct3D에서 CPU로 전환한 표면도 웹뷰 합성 자리를 잃는다.
