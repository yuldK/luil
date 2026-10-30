#pragma once

#include "include/core/SkTypeface.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
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
    // 등록한 앱 가족(`register_font_family`)도 이름으로 찾으며 설치된 같은 이름보다 앞선다.
    // 앱 시작 전에 지정한다. 이미 생성된 화면과 글꼴 객체는 자동으로 갱신하지 않는다.
    void set_ui_typeface_families(std::vector<std::u8string> families);

    // 파일·바이트로 읽은 face 여럿을 OS에 설치하지 않고 한 가족 이름으로 등록한다.
    // 등록한 이름은 설치된 가족처럼 쓰인다 — 기본 UI 글꼴 목록, 설정의 글꼴 가족(`font_settings`), 설치 목록
    // (`installed_font_families`)에 나오고, 굵기·기울임(`font_weight`·`font_slant`)을 요청하면 가족 안에서 가장 가까운
    // face를 고른다. 가변 글꼴 face 하나만 넣어도 `wght` 축으로 모든 굵기를 낸다.
    //  - 같은 이름을 다시 등록하면 face 목록을 바꾼다. nullptr face는 버리고, 남은 face가 없으면 등록을 지운다.
    //  - 빈 이름이나 NUL이 든 이름은 무시한다. 이름 비교는 정확히 같은 글자만 같다.
    //  - 글꼴 cache를 비운다. 이미 생성된 화면은 다음 배치부터 새 가족으로 그린다.
    void register_font_family(std::u8string family, std::vector<sk_sp<SkTypeface>> faces);
    // 등록을 지운다. 없는 이름이면 아무 일도 하지 않는다.
    void unregister_font_family(std::u8string_view family);
}
