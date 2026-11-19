#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace luil {
    struct subtitle_cue
    {
        int start_ms {};
        int end_ms {};
        std::u8string text {};
    };

    // UTF-8 SRT의 여러 줄과 겹치는 자막을 파일 순서대로 보존한다.
    [[nodiscard]] std::vector<subtitle_cue> read_srt(const std::filesystem::path& path);
    // 시작 시각은 포함하고 종료 시각은 제외한다. 겹치는 자막은 줄을 나눠 돌려준다.
    [[nodiscard]] std::u8string subtitle_at(const std::vector<subtitle_cue>& cues, std::chrono::milliseconds position);
}
