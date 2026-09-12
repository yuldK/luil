#pragma once

#include "include/core/SkTypeface.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace luil {
    // OS에 설치하지 않고 글꼴을 읽는다. 실패하거나 face_index가 잘못되면 nullptr다.
    // 반환값이 데이터를 소유하므로 원본 바이트·파일은 이후 유지할 필요가 없다.
    // 앱에서 한 번 읽어 보관하고 SkFont나 glyph_config에 공유한다.
    [[nodiscard]] sk_sp<SkTypeface> load_typeface_bytes(std::span<const std::uint8_t> bytes, int face_index = 0);
    [[nodiscard]] sk_sp<SkTypeface> load_typeface_file(const std::filesystem::path& path, int face_index = 0);

    // 실행 파일에 내장된 Codicons다. 리소스가 없으면 nullptr다.
    [[nodiscard]] sk_sp<SkTypeface> load_codicon_typeface();

    // 설치된 글꼴 가족을 UTF-8 이름 순으로 돌려준다. 중복은 없고 조회 실패 시 빈 목록이다.
    [[nodiscard]] std::vector<std::u8string> installed_font_families();

    // 기본 UI 글꼴을 앞에서부터 찾는다. 빈 목록이나 모두 없는 이름이면 Segoe UI다.
    // 앱 시작 전에 지정한다. 이미 생성된 화면과 글꼴 객체는 자동으로 갱신하지 않는다.
    void set_ui_typeface_families(std::vector<std::u8string> families);
} // namespace luil
