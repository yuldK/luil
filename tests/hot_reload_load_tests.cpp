#include "luil/app/hot_reload.h"

#include "host/hot_reload_load.h"
#include "hot_reload_test_folder.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 파일 읽기, 한도, 오류 줄과 감시 묶음 가르기를 임시 폴더의 파일로 본다 (hot-reload-design.md의 "확인").
// 플랫폼 감시자 없이 core만으로 돌아 Windows와 기기 셸 모두에서 돈다.

// 시험 실행 파일에 실은 바른 스타일과 마크업이다. 기기에는 소스 트리가 없다.
extern const std::uint8_t luil_test_base_style[];
extern const std::size_t luil_test_base_style_size;
extern const std::uint8_t luil_test_markup_sample[];
extern const std::size_t luil_test_markup_sample_size;

namespace {
    using hot_reload_test::scratch_folder;
    using hot_reload_test::write_file;
    using luil::file_change;
    using luil::file_change_batch;
    using luil::file_change_kind;
    using luil::hot_reload_file;
    using luil::hot_reload_kind;
    using luil::hot_reload_limits;
    using luil::hot_reload_status;
    using luil::hot_reload_update;

    [[nodiscard]] std::u8string_view base_style() noexcept
    {
        return { reinterpret_cast<const char8_t*>(luil_test_base_style), luil_test_base_style_size };
    }

    [[nodiscard]] std::u8string_view markup_sample() noexcept
    {
        return { reinterpret_cast<const char8_t*>(luil_test_markup_sample), luil_test_markup_sample_size };
    }

    [[nodiscard]] hot_reload_update load(const std::filesystem::path& path, const hot_reload_kind kind, const hot_reload_limits& limits = {})
    {
        return luil::load_hot_reload_file({ path.u8string(), kind }, limits);
    }

    [[nodiscard]] file_change_batch batch_of(file_change change)
    {
        file_change_batch batch {};
        batch.ticket = luil::file_watch_ticket { 7 };
        batch.changes.push_back(std::move(change));
        return batch;
    }
}

TEST_CASE("A valid style file loads the same value as parsing its text", "[hot_reload]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"style.json") };
    write_file(path, base_style());

    const hot_reload_update update { load(path, hot_reload_kind::style) };
    const luil::style_parse_result parsed { luil::parse_ui_style(base_style()) };
    REQUIRE(parsed.ok());
    REQUIRE(update.status == hot_reload_status::loaded);
    REQUIRE(update.kind == hot_reload_kind::style);
    REQUIRE(update.path == path.u8string());
    REQUIRE(update.style != nullptr);
    REQUIRE(*update.style == *parsed.style);
    REQUIRE(update.markup == nullptr);
    REQUIRE(update.text == base_style());
    REQUIRE(update.errors.empty());
    REQUIRE(update.style_errors.empty());
    REQUIRE_FALSE(update.ticket);
}

TEST_CASE("A broken style file is rejected and its errors are the parser's description", "[hot_reload]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"style.json") };
    // 반쯤 쓴 파일이다. 문법 오류로 막힌다.
    const std::u8string partial { base_style().substr(0, base_style().size() / 2) };
    write_file(path, partial);

    hot_reload_update update { load(path, hot_reload_kind::style) };
    REQUIRE(update.status == hot_reload_status::rejected);
    REQUIRE(update.style == nullptr);
    REQUIRE(update.text == partial);
    REQUIRE(update.style_errors.size() == 1);
    REQUIRE(update.style_errors.front().kind == luil::style_error_kind::syntax);
    REQUIRE(update.errors == std::vector<std::u8string> { luil::describe(update.style_errors.front()) });

    // 오류가 여럿이면 모두 한 줄씩이다.
    write_file(path, u8"{ \"dark\": 1, \"light\": 2 }");
    update = load(path, hot_reload_kind::style);
    REQUIRE(update.status == hot_reload_status::rejected);
    REQUIRE(update.style_errors.size() > 1);
    REQUIRE(update.errors.size() == update.style_errors.size());
    for (std::size_t index { 0 }; index < update.errors.size(); ++index)
        REQUIRE(update.errors[index] == luil::describe(update.style_errors[index]));

    // 빈 파일도 문법 오류다.
    write_file(path, u8"");
    update = load(path, hot_reload_kind::style);
    REQUIRE(update.status == hot_reload_status::rejected);
    REQUIRE(update.text.empty());
}

TEST_CASE("A style that is missing, a folder or over the limit has no value", "[hot_reload]")
{
    const scratch_folder folder {};
    hot_reload_update update { load(folder.file(u8"style.json"), hot_reload_kind::style) };
    REQUIRE(update.status == hot_reload_status::missing);
    REQUIRE(update.errors == std::vector<std::u8string> { u8"File not found: style.json" });
    REQUIRE(update.text.empty());

    // 부모 폴더가 없어도 없는 것이다.
    update = load(folder.file(u8"no-folder/style.json"), hot_reload_kind::style);
    REQUIRE(update.status == hot_reload_status::missing);

    update = load(folder.path(), hot_reload_kind::style);
    REQUIRE(update.status == hot_reload_status::unreadable);
    REQUIRE(update.errors.size() == 1);
    REQUIRE(update.errors.front().starts_with(u8"Not a regular file: "));

    // 한도와 같은 크기는 읽고, 1바이트 넘으면 열지 않는다.
    const std::filesystem::path path { folder.file(u8"style.json") };
    write_file(path, base_style());
    hot_reload_limits limits {};
    limits.max_bytes = base_style().size();
    update = load(path, hot_reload_kind::style, limits);
    REQUIRE(update.status == hot_reload_status::loaded);

    limits.max_bytes = base_style().size() - 1;
    update = load(path, hot_reload_kind::style, limits);
    REQUIRE(update.status == hot_reload_status::too_large);
    REQUIRE(update.style == nullptr);
    REQUIRE(update.text.empty());
    REQUIRE(update.errors.size() == 1);
    REQUIRE(update.errors.front().starts_with(u8"File too large: style.json ("));

    // 경로로 옮길 수 없는 글과 NUL이 든 글은 던지지 않고 읽지 못한 것이다.
    hot_reload_file odd {};
    odd.path = path.u8string() + std::u8string { u8'\0' } + u8"x";
    REQUIRE(luil::load_hot_reload_file(odd).status == hot_reload_status::unreadable);
    odd.path = std::u8string { static_cast<char8_t>(0xFF), static_cast<char8_t>(0xFE) };
    const hot_reload_status invalid { luil::load_hot_reload_file(odd).status };
    REQUIRE((invalid == hot_reload_status::unreadable || invalid == hot_reload_status::missing));
}

TEST_CASE("Markup files load, reject and respect the smaller of the two limits", "[hot_reload]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"markup.xml") };
    write_file(path, markup_sample());

    hot_reload_update update { load(path, hot_reload_kind::markup) };
    REQUIRE(update.status == hot_reload_status::loaded);
    REQUIRE(update.kind == hot_reload_kind::markup);
    REQUIRE(update.markup != nullptr);
    REQUIRE(update.style == nullptr);
    REQUIRE(update.text == markup_sample());
    REQUIRE(update.errors.empty());

    // 닫지 않은 태그다.
    const std::u8string broken { u8"<stack>" };
    write_file(path, broken);
    update = load(path, hot_reload_kind::markup);
    REQUIRE(update.status == hot_reload_status::rejected);
    REQUIRE(update.markup == nullptr);
    REQUIRE(update.text == broken);
    REQUIRE(update.markup_errors.empty() == false);
    REQUIRE(update.errors.size() == update.markup_errors.size());
    REQUIRE(update.errors.front() == luil::describe(update.markup_errors.front()));

    update = load(folder.file(u8"none.xml"), hot_reload_kind::markup);
    REQUIRE(update.status == hot_reload_status::missing);
    update = load(folder.path(), hot_reload_kind::markup);
    REQUIRE(update.status == hot_reload_status::unreadable);

    write_file(path, markup_sample());
    hot_reload_limits limits {};
    limits.max_bytes = markup_sample().size() - 1;
    REQUIRE(load(path, hot_reload_kind::markup, limits).status == hot_reload_status::too_large);
    limits = {};
    limits.markup.max_bytes = markup_sample().size() - 1;
    update = load(path, hot_reload_kind::markup, limits);
    REQUIRE(update.status == hot_reload_status::too_large);
    REQUIRE(update.markup_errors.empty());
    // 스타일은 마크업 한도를 보지 않는다.
    REQUIRE(load(path, hot_reload_kind::style, limits).status == hot_reload_status::rejected);
    limits.markup.max_bytes = markup_sample().size();
    REQUIRE(load(path, hot_reload_kind::markup, limits).status == hot_reload_status::loaded);

    // 해석 한도는 파서가 막는다. 값이 없고 이유는 파서의 줄이다.
    limits = {};
    limits.markup.max_nodes = 1;
    update = load(path, hot_reload_kind::markup, limits);
    REQUIRE(update.status == hot_reload_status::rejected);
    REQUIRE(update.markup_errors.front().kind == luil::ui_markup_error_kind::too_large);
}

TEST_CASE("A batch ends the watch or is dropped by its end", "[hot_reload]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"style.json") };
    write_file(path, base_style());
    const hot_reload_file file { path.u8string(), hot_reload_kind::style };

    file_change_batch lost { batch_of(file_change { file_change_kind::modified, u8"style.json", {} }) };
    lost.end = luil::file_watch_end::lost;
    lost.error = u8"The watched folder was removed.";
    const std::optional<hot_reload_update> gone { luil::read_hot_reload_batch(lost, file, {}) };
    REQUIRE(gone.has_value());
    REQUIRE(gone->status == hot_reload_status::lost);
    REQUIRE(gone->ticket == lost.ticket);
    REQUIRE(gone->path == file.path);
    REQUIRE(gone->errors == std::vector<std::u8string> { lost.error });
    REQUIRE(gone->style == nullptr);

    file_change_batch removed { batch_of(file_change { file_change_kind::modified, u8"style.json", {} }) };
    removed.end = luil::file_watch_end::removed;
    REQUIRE_FALSE(luil::read_hot_reload_batch(removed, file, {}).has_value());
}

TEST_CASE("A batch that says read again reads the file", "[hot_reload]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"style.json") };
    write_file(path, base_style());
    const hot_reload_file file { path.u8string(), hot_reload_kind::style };

    const auto expect_loaded = [&file](const file_change_batch& batch) {
        const std::optional<hot_reload_update> update { luil::read_hot_reload_batch(batch, file, {}) };
        REQUIRE(update.has_value());
        REQUIRE(update->status == hot_reload_status::loaded);
        REQUIRE(update->ticket == batch.ticket);
        REQUIRE(update->text == base_style());
    };
    expect_loaded(batch_of({ file_change_kind::created, u8"style.json", {} }));
    expect_loaded(batch_of({ file_change_kind::modified, u8"style.json", {} }));
    expect_loaded(batch_of({ file_change_kind::renamed, u8"style.json", u8"style.json.tmp" }));

    // 넘쳐서 항목이 없어도 다시 읽는다.
    file_change_batch rescan {};
    rescan.ticket = luil::file_watch_ticket { 7 };
    rescan.rescan = true;
    expect_loaded(rescan);

    // Windows는 이름을 디스크의 표기로 알린다.
    expect_loaded(batch_of({ file_change_kind::modified, u8"STYLE.JSON", {} }));
    expect_loaded(batch_of({ file_change_kind::renamed, u8"style.json", u8"STYLE.JSON" }));

    // 다시 읽으라는 묶음이어도 파일이 없으면 없는 것이다.
    std::filesystem::remove(path);
    const std::optional<hot_reload_update> update { luil::read_hot_reload_batch(batch_of({ file_change_kind::modified, u8"style.json", {} }), file, {}) };
    REQUIRE(update.has_value());
    REQUIRE(update->status == hot_reload_status::missing);
}

TEST_CASE("A batch that says the file is gone does not read it", "[hot_reload]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"style.json") };
    // 파일이 그대로 있어야 읽지 않았음을 안다.
    write_file(path, base_style());
    const hot_reload_file file { path.u8string(), hot_reload_kind::style };

    const auto expect_missing = [&file](const file_change_batch& batch) {
        const std::optional<hot_reload_update> update { luil::read_hot_reload_batch(batch, file, {}) };
        REQUIRE(update.has_value());
        REQUIRE(update->status == hot_reload_status::missing);
        REQUIRE(update->ticket == batch.ticket);
        REQUIRE(update->text.empty());
        REQUIRE(update->errors == std::vector<std::u8string> { u8"File not found: style.json" });
    };
    expect_missing(batch_of({ file_change_kind::removed, u8"style.json", {} }));
    expect_missing(batch_of({ file_change_kind::renamed, u8"style.json.bak", u8"style.json" }));
    expect_missing(batch_of({ file_change_kind::removed, u8"Style.JSON", {} }));
}

TEST_CASE("A batch with only other names gives no result", "[hot_reload]")
{
    const scratch_folder folder {};
    const std::filesystem::path path { folder.file(u8"style.json") };
    write_file(path, base_style());
    const hot_reload_file file { path.u8string(), hot_reload_kind::style };

    REQUIRE_FALSE(luil::read_hot_reload_batch(batch_of({ file_change_kind::modified, u8"other.json", {} }), file, {}).has_value());
    REQUIRE_FALSE(luil::read_hot_reload_batch(batch_of({ file_change_kind::removed, u8"other.json", {} }), file, {}).has_value());
    REQUIRE_FALSE(luil::read_hot_reload_batch(batch_of({ file_change_kind::renamed, u8"b.json", u8"a.json" }), file, {}).has_value());
    REQUIRE_FALSE(luil::read_hot_reload_batch(file_change_batch {}, file, {}).has_value());
}
