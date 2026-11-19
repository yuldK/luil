#include "video/scene.h"

#include <windows.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <cstdio>

namespace video_demo {
    namespace {
        using video_strings::key;

        constexpr std::array<std::u8string_view, 3> formats { u8".mp4", u8".avi", u8".ts" };

        std::u8string clock_text(std::chrono::milliseconds position)
        {
            const auto total { std::chrono::duration_cast<std::chrono::seconds>(position).count() };
            const auto digits { std::to_string(total / 60) + (total % 60 < 10 ? ":0" : ":") + std::to_string(total % 60) };
            return { digits.begin(), digits.end() };
        }

        class driver final : public luil::logic_driver
        {
        public:
            driver(const luil::string_lookup strings, std::filesystem::path samples, std::filesystem::path video, std::filesystem::path subtitle, std::filesystem::path log_path)
                : strings_ { strings }
                , samples_ { std::move(samples) }
                , video_ { std::move(video) }
                , subtitle_ { std::move(subtitle) }
                , log_path_ { std::move(log_path) }
            {
            }

            void start() override
            {
                if (log_path_.empty() == false && _wfopen_s(&frame_log_, log_path_.c_str(), L"wx") == 0)
                {
                    LARGE_INTEGER frequency {};
                    QueryPerformanceFrequency(&frequency);
                    std::fprintf(frame_log_, "# qpc_frequency,%lld\npts_100ns,transfer_qpc\n", frequency.QuadPart);
                }
                player_ = std::make_unique<luil::win32::video_frame_player>();
                luil::image_decode_error error {};
                poster_ = luil::load_image_file((samples_ / "ridge.jpg").u8string(), error);
                open();
            }

            ~driver() override
            {
                if (frame_log_ != nullptr)
                    std::fclose(frame_log_);
            }

            void handle(luil::app_message message) override
            {
                publish_for_message_ = true;
                if (const auto* size { message.get<luil::window_metrics>() }; size != nullptr)
                    metrics_ = *size;
                if (const auto* choice { message.get<sample_choice>() }; choice && closed_.load() == false)
                {
                    if (choice->extension != format_)
                        select_sample(choice->extension);
                    return;
                }
                if (const auto* appearance { message.get<luil::system_appearance>() }; appearance != nullptr)
                    translucent_overlays_ = appearance->transparency && appearance->high_contrast == false;
                const auto* action { message.get<command>() };
                if (action == nullptr || closed_.load())
                    return;
                switch (*action)
                {
                case command::play:
                    player_->play();
                    break;
                case command::stop:
                    player_->stop();
                    break;
                case command::next_sample:
                {
                    const auto it { std::find(formats.begin(), formats.end(), format_) };
                    const auto next { it == formats.end() || std::next(it) == formats.end() ? formats.begin() : std::next(it) };
                    select_sample(*next);
                    break;
                }
                case command::change_theme:
                    if (theme_ == luil::theme_preference::system)
                        theme_ = luil::theme_preference::dark;
                    else if (theme_ == luil::theme_preference::dark)
                        theme_ = luil::theme_preference::light;
                    else
                        theme_ = luil::theme_preference::system;
                    break;
                case command::close:
                    if (frame_log_ != nullptr)
                    {
                        const auto statistics { player_->statistics() };
                        if (statistics.dropped)
                            std::fprintf(frame_log_, "# engine_frames_dropped,%u\n", *statistics.dropped);
                        if (statistics.rendered)
                            std::fprintf(frame_log_, "# engine_frames_rendered,%u\n", *statistics.rendered);
                    }
                    player_.reset();
                    closed_.store(true);
                    break;
                }
            }

            std::optional<std::chrono::steady_clock::time_point> next_tick() override
            {
                if (player_ && player_->needs_tick())
                    return std::chrono::steady_clock::now() + std::chrono::milliseconds { 4 };
                return std::nullopt;
            }

            void tick(std::chrono::steady_clock::time_point) override
            {
                if (player_ == nullptr)
                    return;
                const auto previous { player_->image().backing() };
                player_->update();
                if (frame_log_ != nullptr && player_->image().valid() && previous != player_->image().backing())
                {
                    LARGE_INTEGER counter {};
                    QueryPerformanceCounter(&counter);
                    std::fprintf(frame_log_, "%lld,%lld\n", player_->frame_timestamp_100ns(), counter.QuadPart);
                }
            }

            std::shared_ptr<const luil::ui_frame> make_frame() override
            {
                std::u8string text {};
                const auto position { player_ ? player_->position() : std::chrono::milliseconds {} };
                const auto second { std::chrono::duration_cast<std::chrono::seconds>(position) };
                if (player_)
                {
                    text = luil::subtitle_at(cues_, position);
                    const auto status { status_text() };
                    // 같은 영상을 반복 게시하면 표시 큐가 차서 뒤의 새 영상까지 기다리게 된다.
                    if (publish_for_message_ == false && last_image_.backing() == player_->image().backing()
                        && last_status_ == status && last_subtitle_ == text && last_second_ == second)
                        return {};
                    last_image_ = player_->image();
                    last_second_ = second;
                    last_status_ = status;
                    last_subtitle_ = text;
                }
                publish_for_message_ = false;
                player_view view { .strings = strings_ };
                view.image = last_image_.valid() || format_.empty() ? last_image_ : poster_;
                view.title = format_.empty() ? video_.filename().u8string() : view.text(key::ridge_title);
                view.detail = format_.empty() ? video_.parent_path().u8string() : video_.filename().u8string()
                    + (format_ == u8".avi" ? u8"   ·   MJPEG / PCM   ·   640 × 360" : u8"   ·   H.264 / AAC   ·   960 × 540")
                ;
                view.format = format_;
                view.state = player_ ? player_->state() : luil::win32::media_state::empty;
                view.status = last_status_;
                view.subtitle = std::move(text);
                if (cues_.empty())
                    view.subtitle_file = view.text(key::no_subtitles);
                else
                {
                    const auto file { subtitle_.filename().u8string() };
                    view.subtitle_file = video_strings::format(strings_, key::subtitle_file, std::array { luil::string_argument { u8"file", file } });
                }
                view.clock = clock_text(position);
                if (format_.empty() == false)
                    view.clock += u8" / 0:08";
                view.theme = theme_;
                view.translucent_overlays = translucent_overlays_;
                auto root { build_player(view, metrics_) };
                auto frame { std::make_shared<luil::ui_frame>() };
                frame->appearance.theme = theme_;
                frame->prefer_latest_presentation = player_ && player_->needs_tick();
                frame->tree = std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root)
                    , { 0, 0, metrics_.width, metrics_.height }, metrics_.scale
                ));
                return frame;
            }

            luil::app_message make_close_message() override
            {
                return luil::app_message { command::close };
            }

            bool shutdown_completed() const override
            {
                return closed_.load();
            }

        private:
            void select_sample(const std::u8string_view extension)
            {
                if (std::find(formats.begin(), formats.end(), extension) == formats.end())
                    return;
                video_ = samples_ / "ridge";
                video_.replace_extension(extension);
                subtitle_ = samples_ / "ridge.srt";
                open();
            }

            void open()
            {
                format_.clear();
                for (const auto extension : formats)
                {
                    auto sample { samples_ / "ridge" };
                    sample.replace_extension(extension);
                    if (video_.lexically_normal() == sample.lexically_normal())
                        format_ = extension;
                }
                cues_ = luil::read_srt(subtitle_);
                if (log_path_.empty() || frame_log_ != nullptr)
                    player_->open(video_);
            }

            std::u8string_view status_text() const
            {
                if (log_path_.empty() == false && frame_log_ == nullptr)
                    return video_strings::text(strings_, key::log_error);
                using luil::win32::media_state;
                switch (player_->state())
                {
                case media_state::opening:
                    return video_strings::text(strings_, key::opening);
                case media_state::ready:
                    return video_strings::text(strings_, key::ready);
                case media_state::playing:
                    return video_strings::text(strings_, key::playing);
                case media_state::stopped:
                    return video_strings::text(strings_, key::stopped);
                case media_state::ended:
                    return video_strings::text(strings_, key::ended);
                case media_state::failed:
                    return player_->error();
                default:
                    return {};
                }
            }

            luil::string_lookup strings_ {};
            luil::ui_image poster_ {};
            std::u8string format_ {};
            luil::theme_preference theme_ { luil::theme_preference::system };
            std::FILE* frame_log_ {};
            std::filesystem::path samples_ {};
            std::filesystem::path video_ {};
            std::filesystem::path subtitle_ {};
            std::filesystem::path log_path_ {};
            std::vector<luil::subtitle_cue> cues_ {};
            std::chrono::seconds last_second_ {};
            luil::ui_image last_image_ {};
            std::u8string last_status_ {};
            std::u8string last_subtitle_ {};
            bool translucent_overlays_ { true };
            bool publish_for_message_ { true };
            std::unique_ptr<luil::win32::video_frame_player> player_ {};
            luil::window_metrics metrics_ { luil::make_window_metrics(880, 610, 1) };
            std::atomic<bool> closed_ {};
        };

        class delegate final : public luil::win32::window_delegate
        {
        public:
            luil::app_message make_system_appearance_message(const luil::system_appearance& appearance) override
            {
                return luil::app_message { appearance };
            }

            luil::app_message make_window_metrics_message(const float width, const float height, const float scale) override
            {
                return luil::app_message { luil::make_window_metrics(width, height, scale) };
            }
        };
    }
}

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    luil::win32::enable_per_monitor_dpi_awareness();
    const luil::win32::com_sta_scope com {};
    wchar_t module[32768] {};
    const auto length { GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module))) };
    if (length == 0 || length == std::size(module))
        return 1;
    const auto samples { std::filesystem::path { module }.parent_path() / "video" };
    std::filesystem::path source { samples / "ridge.mp4" };
    auto renderer { luil::renderer_mode::automatic };
    std::filesystem::path subtitle {};
    std::filesystem::path log_path {};
    if (const auto args { luil::win32::command_line_arguments() }; args)
    {
        for (const auto& arg : *args)
        {
            if (arg.starts_with(u8"--renderer="))
            {
                if (const auto mode { luil::parse_renderer_mode(arg.substr(11)) }; mode)
                    renderer = *mode;
                else
                    return 1;
            }
            if (arg.starts_with(u8"--video="))
            {
                if (arg.size() == 8)
                    return 1;
                std::error_code error {};
                source = std::filesystem::absolute(std::filesystem::path { arg.substr(8) }, error);
                if (error)
                    return 1;
            }
            if (arg.starts_with(u8"--frame-log="))
                log_path = std::filesystem::path { arg.substr(12) };
            if (arg.starts_with(u8"--srt="))
                subtitle = std::filesystem::path { arg.substr(6) };
        }
    }
    if (subtitle.empty())
    {
        subtitle = source;
        subtitle.replace_extension(".srt");
    }
    const auto languages { luil::user_ui_languages() };
    luil::set_text_languages(languages);
    const auto strings { luil::select_strings(video_strings::table, languages) };
    video_demo::driver driver { strings, samples, source, subtitle, log_path };
    video_demo::delegate delegate {};
    luil::win32::window_config config {};
    config.class_name = L"Luil.VideoDemo";
    config.caption.title = video_strings::text(strings, video_strings::key::window_title);
    config.renderer = renderer;
    config.initial_width = 880;
    config.initial_height = 610;
    luil::win32::window_environment environment {};
    environment.driver = &driver;
    environment.delegate = &delegate;
    return luil::win32::run_application_window(config, environment);
}
