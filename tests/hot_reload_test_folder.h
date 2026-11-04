#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

// 두 핫 리로드 시험이 함께 쓰는 임시 폴더다. 한 파일(hot_reload_load_tests.cpp)이 core test에 들어 OS 헤더를 쓰지 않는다.
namespace hot_reload_test {
    // 기기 셸에는 쓸 수 있는 임시 폴더가 정해져 있지 않다. 실행 파일이 도는 자리(adb_run.cmake의 기기 폴더) 아래에 둔다.
    [[nodiscard]] inline std::filesystem::path scratch_base()
    {
#if defined(__ANDROID__)
        return std::filesystem::current_path();
#else
        return std::filesystem::temp_directory_path();
#endif
    }

    class scratch_folder
    {
    public:
        scratch_folder()
            : path_ { scratch_base() / ("luil-hot-reload-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++counter_)) }
        {
            std::filesystem::create_directories(path_);
        }
        scratch_folder(const scratch_folder&) = delete;
        scratch_folder& operator=(const scratch_folder&) = delete;
        ~scratch_folder()
        {
            std::error_code ignored {};
            std::filesystem::remove_all(path_, ignored);
        }

        [[nodiscard]] std::filesystem::path file(const std::u8string_view name) const
        {
            return path_ / std::filesystem::path { name };
        }

        [[nodiscard]] std::u8string text(const std::u8string_view name) const
        {
            return file(name).u8string();
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

    private:
        static inline int counter_ { 0 };
        std::filesystem::path path_ {};
    };

    inline void write_file(const std::filesystem::path& path, const std::u8string_view text)
    {
        std::ofstream file { path, std::ios::binary | std::ios::trunc };
        file.write(reinterpret_cast<const char*>(text.data()), static_cast<std::streamsize>(text.size()));
    }

    // 임시 파일에 다 쓰고 이름을 바꾸는 저장이다. 읽는 쪽은 옛 글 전체나 새 글 전체를 본다.
    inline void save_file(const std::filesystem::path& path, const std::u8string_view text)
    {
        std::filesystem::path temporary { path };
        temporary += ".tmp";
        write_file(temporary, text);
        std::filesystem::rename(temporary, path);
    }
}
