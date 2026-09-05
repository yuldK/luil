#pragma once

#include "include/core/SkRefCnt.h"

#include <cstddef>
#include <string>
#include <vector>

class SkTypeface;

namespace luil::win32 {
    struct embedded_resource_view
    {
        const void* data { nullptr };
        std::size_t size { 0 };
    };

    [[nodiscard]] embedded_resource_view find_embedded_resource(int resource_id) noexcept;
    [[nodiscard]] sk_sp<SkTypeface> load_codicon_typeface();
    // 기본 UI 글꼴로 시도할 시스템 가족 이름 목록이다.
    // 앱이 시작 시 한 번 지정하며 (예: 한국어 UI라면 "Malgun Gothic" 우선),
    // 지정하지 않으면 "Segoe UI"만 시도한다.
    void set_ui_typeface_families(std::vector<std::u8string> families);
    [[nodiscard]] sk_sp<SkTypeface> load_ui_typeface();
    [[nodiscard]] bool verify_embedded_resources(std::u8string& error);
} // namespace luil::win32
