#pragma once

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/win32/renderer_policy.h"

#include <string>

class SkCanvas;
class SkTypeface;

namespace luil {
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
        bool maximized { false };

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
    };

    // frame을 그린다.
    // 팔레트는 theme·accent에서 합성하고,
    // tree가 있으면 tree가 화면 전체를 그린다 (tooltip·drag 표시 포함).
    void draw_frame(SkCanvas& canvas, SkTypeface* codicon_typeface, SkTypeface* ui_typeface, const frame_state& state);
} // namespace luil
