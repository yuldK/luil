#pragma once

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/win32/renderer_policy.h"

#include <span>
#include <string>

class SkCanvas;
class SkTypeface;

namespace luil {
    // 물리 픽셀 사각형이다 (창 client 기준).
    struct pixel_rect
    {
        int x { 0 };
        int y { 0 };
        int width { 0 };
        int height { 0 };

        [[nodiscard]] bool operator==(const pixel_rect&) const noexcept = default;
    };

    // 렌더러가 한 frame을 그리는 데 필요한 전부다.
    // UI thread가 매 frame 채우고,
    // 값의 근원은 logic이 게시한 tree·외양 선호와 input thread의 상호작용 발행본이다.
    struct frame_state
    {
        int width { 0 };
        int height { 0 };
        float dpi_scale { 1.0F };
        renderer_backend backend { renderer_backend::cpu };
        bool used_fallback { false };
        color_theme theme { color_theme::dark };
        // 앱 설정이 고른 키 컬러 id다.
        // 비어 있거나 목록에 없으면 기본색이다.
        std::u8string accent_id {};
        // theme이 고대비일 때 팔레트를 합성할 시스템 색이다.
        // platform이 OS에서 읽어 채운다 (기본값은 검정 바탕 fallback).
        high_contrast_colors high_contrast {};
        // 앱이 frame에 실은 스타일이다 (`ui_frame::style`). 없으면 내장 스타일이다.
        // 포인터는 렌더 호출 동안만 유효하면 된다 (tree와 같은 규칙).
        const ui_style* style { nullptr };
        bool maximized { false };
        // 테두리 없는 전체 화면인가다.
        // 최대화와 마찬가지로 view snapshot에 없는 창 상태라 표면이 채운다 —
        // 채우는 곳은 그 상태를 든 자리(`caption_surface::prepare_frame`)이고,
        // 거기서 둘을 한 모드에서 함께 세워 둘이 동시에 참이 되지 않는다.
        bool fullscreen { false };
        // 표면 둘레에 1px 테두리를 긋는가다 (`tooltip_border`).
        // popup 표면이 켠다 (`popup_surface::prepare_frame`) — 다른 화면 위에 뜨는 판이라
        // 경계가 있어야 아래 화면과 갈린다. tree를 다 그린 뒤 그 위에 긋는다.
        bool border { false };

        // input thread가 게시한 상호작용 상태다.
        // caption의 비클라이언트 hover는 UI thread가 게시 전에 합쳐 둔다.
        interaction_snapshot interaction {};

        // 그릴 tree다.
        // 포인터는 렌더 호출 동안만 유효하면 된다.
        // 없으면 창 배경만 칠한다 (조립 전·smoke의 기본 화면).
        const ui_tree* tree { nullptr };

        // 고정폭 본문의 글꼴이다.
        // 없으면 UI 글꼴을 쓴다.
        // 글꼴 미리 보기는 `fonts`가 해석한다.
        SkTypeface* code_typeface { nullptr };
        const font_resolver* fonts { nullptr };

        // 이 frame에서 알파 0으로 비울 사각형들이다 (물리 픽셀, client 기준).
        //
        // tree를 **다 그린 뒤** 비운다. 그 자리 아래에 웹뷰 visual이 있어 비워진
        // 만큼 페이지가 드러난다 (webview-composition-design.md).
        //  - 그리기 도중에 비우면 뒤에 그려지는 것이 도로 덮는다. 반대로 다 그린
        //    뒤에 비우면 **그 자리에 그려진 것이 전부 지워진다** — 그래서 무언가
        //    그 위를 덮어야 하는 frame에서는 목록에 아예 담기지 않는다. 담을지
        //    말지는 `plan_webview_layout`이 판정한다.
        //  - 비어 있는 것이 보통이다. 웹뷰를 싣지 않은 앱은 이 경로를 지나지 않는다.
        std::span<const pixel_rect> holes {};
    };

    // frame이 쓸 스타일이다 — 실린 것이 없으면 내장 스타일이다.
    [[nodiscard]] const ui_style& frame_style(const frame_state& state) noexcept;
    // frame의 팔레트다.
    // 고대비는 시스템 색으로, 나머지는 스타일의 중립 색 위에 키 컬러를 얹어 합성한다.
    // 그리기(`draw_frame`)와 창 테두리(DWM)가 **같은 함수**로 같은 팔레트를 본다.
    [[nodiscard]] ui_color_palette frame_palette(const frame_state& state) noexcept;

    // frame을 그린다.
    // 팔레트는 `frame_palette`가 합성하고,
    // tree가 있으면 tree가 화면 전체를 그린다 (tooltip·drag 표시 포함).
    void draw_frame(SkCanvas& canvas, SkTypeface* codicon_typeface, SkTypeface* ui_typeface, const frame_state& state);
} // namespace luil
