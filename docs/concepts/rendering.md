# 렌더링

UI 스레드는 logic이 게시한 `ui_frame`과 input의 `interaction_snapshot`을 표면의 크기·DPI·창 상태와 결합한다. 내부 [`frame_state`](../../src/win32/frame_state.h)는 물리 픽셀 크기, `dpi_scale`, backend 정보, 테마·accent·고대비 색, maximized 상태, interaction, tree와 글꼴 포인터, 웹뷰 영역인 `holes`를 담는다. tree와 글꼴 포인터는 렌더 호출 동안 유효해야 한다.

[`draw_frame()`](../../src/win32/frame_state.cpp)이 테마와 accent에서 팔레트를 만들고 `draw_context`를 구성한다. tree가 없으면 배경만 그린다. tree가 있으면 element와 초점 테·tooltip·drag 표시를 그린 뒤 `holes`의 물리 픽셀 사각형을 알파 0으로 비운다.

## Backend와 실패 처리

내부 [`skia_renderer`](../../src/win32/skia_renderer.h)의 가상 함수는 `backend()`, `resize()`, `render()`, `underlay()`다. 제시는 각 backend의 `render()` 안에서 처리한다. CPU backend는 Skia raster 결과를 GDI로 제시하고, Direct3D backend는 D3D12와 Skia Ganesh로 그린 swap chain을 DirectComposition에 붙인다.

`renderer_host`의 정책은 [`renderer_mode`](../../include/luil/win32/renderer_policy.h)로 정한다.

| 모드 | 동작 |
| --- | --- |
| `cpu` | CPU renderer만 생성한다. |
| `direct3d` | Direct3D 생성·resize·render 실패를 호출자에게 반환한다. |
| `automatic` | Direct3D를 우선하며 생성·resize·render 실패 시 CPU로 전환한다. |

CPU 전환도 실패하면 오류를 반환한다. CPU로 전환한 host가 다음 frame에서 자동으로 Direct3D를 다시 시도하지는 않는다. 전환 시 이전 DirectComposition target을 해제해야 GDI 결과가 화면에 드러난다.

## 다시 그리기

[`window_surface`](../../src/win32/window_surface.cpp)는 frame·interaction 게시, 창 크기·DPI·테마 변경, timer를 무효화 요청으로 모으고 `WM_PAINT`에서 그린다. interaction은 표면 id로 걸러 다른 창의 hover·focus·drag가 섞이지 않게 하고, caption의 비클라이언트 hover를 합친다.

`ui_tree::next_update()`는 보이는 element와 tooltip의 가장 이른 갱신 시각을 반환한다. 플랫폼은 표면들의 예고를 모아 timer를 예약한다. 미래 시각은 그때 다시 그리라는 뜻이고, 현재 이하의 시각은 계속 갱신하라는 뜻이며, `nullopt`는 시간에 따른 그림 변경이 없다는 뜻이다. 앱 상태 자체의 시간 변경은 `logic_driver::next_tick()`과 `tick()`으로 처리한다.

## 웹뷰 합성

`webview_element`는 tree 안에서 웹뷰의 자리와 잘림을 제공한다. 렌더 전에 `apply_webviews()`가 배치와 가림 상태를 계산하고 renderer 아래의 composition visual에 페이지를 배치한다. native tree를 모두 그린 뒤 해당 영역만 비우므로 페이지가 드러난다. native 콘텐츠가 웹뷰 위를 가려야 하는 frame은 배치 계획에서 그 영역을 구멍 목록에서 제외한다.

CPU renderer의 `underlay()`는 null이므로 페이지를 합성할 수 없다. 이 경우와 WebView2를 만들 수 없는 경우에는 tree의 placeholder가 남는다. Direct3D에서 CPU로 전환한 표면도 웹뷰 합성 자리를 잃는다.
