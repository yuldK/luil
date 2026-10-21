#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace luil {
    enum class file_dialog_kind { open, open_multiple, save, folder };
    struct file_filter
    {
        std::u8string label {};
        std::vector<std::u8string> extensions {};
    };
    struct file_dialog_request
    {
        // 같은 id는 실행 중인 앱의 수명 동안 한 번만 연다.
        std::uint64_t id { 0 };
        file_dialog_kind kind { file_dialog_kind::open };
        std::u8string title {};
        std::u8string initial_folder {};
        std::u8string default_name {};
        std::vector<file_filter> filters {};
        std::u8string owner_surface {};
    };
    enum class file_dialog_status { chosen, cancelled, failed, unsupported };
    struct file_dialog_result
    {
        std::uint64_t id { 0 };
        file_dialog_status status { file_dialog_status::cancelled };
        std::vector<std::u8string> paths {};
        std::u8string error {};
    };
} // namespace luil
