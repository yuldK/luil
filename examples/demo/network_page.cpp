#include "demo/network_page.h"

#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/scroll_area_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/strip_element.h"
#include "update_fixtures.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <utility>

namespace demo {
    namespace {
        // 줄 하나의 높이와 결과 칸에 세울 줄 수의 상한이다 (논리 픽셀·줄).
        //  - json은 여덟 줄이면 형태가 보인다. 글은 더 보여 주되 상한을 둔다 —
        //    몸이 64 MiB까지 올 수 있는 자리라(3.15) 줄 수가 곧 element 수다.
        constexpr float preview_line_height { 16.0f };
        constexpr std::size_t json_preview_lines { 8 };
        constexpr std::size_t text_preview_lines { 200 };
        // 한 줄에서 담아 둘 바이트 상한이다. 흘리는 창이 가로로 잘라 주지만,
        // 잘릴 글을 tree에 싣는 것은 그리기가 아니라 메모리의 문제다.
        constexpr std::size_t preview_line_bytes { 400 };
        // 자동 업데이트 구역의 줄 높이와 줄 사이다. 구역 높이는 제목·단추·확인란·상태·진단·적용 방식 여섯 줄을 쌓은 값이다.
        constexpr float update_line_height { 18.0f };
        constexpr float update_section_spacing { 6.0f };
        constexpr float update_section_height { update_line_height * 4.0f + 28.0f + luil::check_row_height + update_section_spacing * 5.0f };

        // 이 플랫폼의 적용 방식을 사람 글로 옮긴다 (auto-update-apply-design.md).
        [[nodiscard]] std::u8string_view apply_mode_text(const luil::update_apply_mode mode) noexcept
        {
            switch (mode)
            {
            case luil::update_apply_mode::replace_executable:
                return u8"실행 파일 바꾸기";
            case luil::update_apply_mode::open_store:
                return u8"스토어 열기";
            }
            return u8"실행 파일 바꾸기";
        }

        // 미리 놓은 과녁 하나다 (단추의 글, 서버의 길, 동사).
        struct preset_target
        {
            std::u8string_view label {};
            std::u8string_view path {};
            bool post { false };
        };

        // 화면의 단추 줄이 곧 서버가 내주는 길의 목록이다 — 두 벌이면 언젠가
        // 한쪽만 늘어나고, 그때 예제가 거짓말을 한다.
        constexpr std::array<preset_target, 10> preset_targets {
            preset_target { u8"상태", u8"/status", false },
            preset_target { u8"그림", u8"/image.png", false },
            preset_target { u8"움직이는 그림", u8"/spinner.gif", false },
            preset_target { u8"글", u8"/text", false },
            preset_target { u8"euc-kr", u8"/euc-kr", false },
            preset_target { u8"HTML", u8"/page", false },
            preset_target { u8"냄새", u8"/octet", false },
            preset_target { u8"POST", u8"/echo", true },
            preset_target { u8"느림", u8"/slow", false },
            preset_target { u8"404", u8"/missing", false },
        };

        [[nodiscard]] std::u8string from_utf8(const std::string_view text)
        {
            return std::u8string { reinterpret_cast<const char8_t*>(text.data()), text.size() };
        }

        [[nodiscard]] std::u8string to_u8_number(const std::uint64_t value)
        {
            return from_utf8(std::to_string(value));
        }

        [[nodiscard]] std::u8string size_text(const std::size_t bytes)
        {
            return to_u8_number(static_cast<std::uint64_t>(bytes)) + u8" B";
        }

        // 주소에서 경로만 뗀다 (머리줄이 "GET /status"로 읽히도록).
        // scheme·host가 없으면 받은 글을 그대로 경로로 본다.
        [[nodiscard]] std::u8string path_of(const std::u8string_view url)
        {
            const std::size_t scheme { url.find(u8"://") };
            const std::size_t host { scheme == std::u8string_view::npos ? std::size_t { 0 } : scheme + 3 };
            const std::size_t slash { url.find(u8'/', host) };
            if (slash == std::u8string_view::npos)
                return u8"/";
            return std::u8string { url.substr(slash) };
        }

        [[nodiscard]] std::u8string_view kind_name(const luil::net::http_body_kind kind) noexcept
        {
            switch (kind)
            {
            case luil::net::http_body_kind::empty:
                return u8"empty";
            case luil::net::http_body_kind::json:
                return u8"json";
            case luil::net::http_body_kind::image:
                return u8"image";
            case luil::net::http_body_kind::text:
                return u8"text";
            case luil::net::http_body_kind::html:
                return u8"html";
            case luil::net::http_body_kind::bytes:
                return u8"bytes";
            }
            return u8"?";
        }

        // 갈래를 무엇으로 정했는가. **이 페이지의 두 번째 요점이라** 머리줄에 함께 적는다.
        [[nodiscard]] std::u8string_view kind_source_name(const luil::net::http_body_kind_source source) noexcept
        {
            switch (source)
            {
            case luil::net::http_body_kind_source::absent:
                return u8"absent";
            case luil::net::http_body_kind_source::declared:
                return u8"declared";
            case luil::net::http_body_kind_source::sniffed:
                return u8"sniffed";
            case luil::net::http_body_kind_source::assumed:
                return u8"assumed";
            }
            return u8"?";
        }

        [[nodiscard]] std::u8string_view error_kind_name(const luil::net::http_error_kind kind) noexcept
        {
            switch (kind)
            {
            case luil::net::http_error_kind::none:
                return u8"none";
            case luil::net::http_error_kind::invalid_url:
                return u8"invalid_url";
            case luil::net::http_error_kind::unsupported_scheme:
                return u8"unsupported_scheme";
            case luil::net::http_error_kind::invalid_header:
                return u8"invalid_header";
            case luil::net::http_error_kind::name_not_resolved:
                return u8"name_not_resolved";
            case luil::net::http_error_kind::cannot_connect:
                return u8"cannot_connect";
            case luil::net::http_error_kind::connection_lost:
                return u8"connection_lost";
            case luil::net::http_error_kind::secure_failure:
                return u8"secure_failure";
            case luil::net::http_error_kind::timed_out:
                return u8"timed_out";
            case luil::net::http_error_kind::cancelled:
                return u8"cancelled";
            case luil::net::http_error_kind::too_many_redirects:
                return u8"too_many_redirects";
            case luil::net::http_error_kind::body_too_large:
                return u8"body_too_large";
            case luil::net::http_error_kind::stopped:
                return u8"stopped";
            case luil::net::http_error_kind::system_error:
                return u8"system_error";
            }
            return u8"?";
        }

        // 바이트 상한에서 자르되 **글자 경계로 물러선다.**
        // 가운데를 자르면 깨진 UTF-8이 되고, 그리기가 그런 글을 통째로 거른다
        // (text/utf8_text.h).
        [[nodiscard]] std::u8string clip_utf8(const std::u8string_view text, const std::size_t limit)
        {
            if (text.size() <= limit)
                return std::u8string { text };
            std::size_t cut { limit };
            while (cut > 0 && (text[cut] & 0xC0) == 0x80)
                --cut;
            return std::u8string { text.substr(0, cut) } + u8" …";
        }

        // 글을 줄로 가른다 (CR은 떼고, 줄 수와 줄 길이에 상한을 건다).
        [[nodiscard]] std::vector<std::u8string> take_lines(const std::u8string_view text, const std::size_t limit)
        {
            std::vector<std::u8string> lines {};
            std::size_t begin { 0 };
            std::size_t total { 0 };
            while (begin <= text.size())
            {
                std::size_t end { text.find(u8'\n', begin) };
                if (end == std::u8string_view::npos)
                    end = text.size();
                std::u8string_view line { text.substr(begin, end - begin) };
                if (line.empty() == false && line.back() == u8'\r')
                    line.remove_suffix(1);
                ++total;
                if (lines.size() < limit)
                    lines.push_back(clip_utf8(line, preview_line_bytes));
                if (end == text.size())
                    break;
                begin = end + 1;
            }
            if (total > limit)
                lines.push_back(u8"… " + to_u8_number(total - limit) + u8"줄 더 있다");
            return lines;
        }

        [[nodiscard]] std::u8string hex_preview(const std::span<const std::uint8_t> bytes)
        {
            constexpr std::u8string_view digits { u8"0123456789abcdef" };
            const std::size_t count { bytes.size() < 16 ? bytes.size() : std::size_t { 16 } };
            std::u8string text {};
            for (std::size_t index = 0; index < count; ++index)
            {
                if (index > 0)
                    text.push_back(u8' ');
                text.push_back(digits[static_cast<std::size_t>(bytes[index] >> 4)]);
                text.push_back(digits[static_cast<std::size_t>(bytes[index] & 0x0F)]);
            }
            return text;
        }

        // 단추가 글을 담기에 넉넉한 폭이다 (논리 픽셀).
        // 배치에 측정 단계가 없으므로 담는 쪽이 잡는다 — 한글은 한 칸, ASCII는
        // 반 칸으로 어림한다.
        [[nodiscard]] float label_width(const std::u8string_view label) noexcept
        {
            float units { 0.0f };
            for (const char8_t character : label)
                if (character < 0x80)
                    units += 0.6f;
                else if ((character & 0xC0) != 0x80)
                    units += 1.0f;
            return 24.0f + 12.0f * units;
        }

        // 실행 파일이 있는 디렉터리다 (자산은 그 옆에 선다).
        // 작업 디렉터리로 찾으면 무엇으로 띄웠느냐에 따라 자리가 달라진다
        // (basics_page.cpp와 같은 판단이고 같은 코드다).
        [[nodiscard]] std::filesystem::path module_directory()
        {
            std::array<wchar_t, MAX_PATH> buffer {};
            const DWORD length { GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size())) };
            if (length == 0 || length >= buffer.size())
                return {};
            return std::filesystem::path { std::wstring { buffer.data(), length } }.parent_path();
        }

        [[nodiscard]] std::vector<std::uint8_t> read_asset(const std::wstring_view name)
        {
            const std::filesystem::path directory { module_directory() };
            if (directory.empty())
                return {};
            std::ifstream file { directory / L"assets" / name, std::ios::binary };
            if (file.is_open() == false)
                return {};
            const std::string blob { std::istreambuf_iterator<char> { file }, std::istreambuf_iterator<char> {} };
            std::vector<std::uint8_t> bytes {};
            bytes.reserve(blob.size());
            for (const char character : blob)
                bytes.push_back(static_cast<std::uint8_t>(character));
            return bytes;
        }

        [[nodiscard]] luil::testing::loopback_response make_body_response(const std::string_view content_type, std::vector<std::uint8_t> body)
        {
            luil::testing::loopback_response response {};
            if (content_type.empty() == false)
                response.headers.push_back({ "Content-Type", std::string { content_type } });
            response.body = std::move(body);
            return response;
        }

        [[nodiscard]] luil::testing::loopback_response make_text_response(const std::string_view content_type, const std::string_view body)
        {
            return make_body_response(content_type, luil::testing::loopback_bytes(body));
        }

        [[nodiscard]] luil::testing::loopback_response make_missing_response(const std::string_view detail)
        {
            // **오류가 아니라 답이다.** 404의 몸에 이유가 실려 있고 갈래는 여전히
            // json이다 (`+json` 접미사를 보는 판정이 여기서 값을 낸다 — 3.6).
            luil::testing::loopback_response response { make_text_response("application/problem+json", detail) };
            response.status = 404;
            return response;
        }

        // 자동 업데이트 구역의 asset 덩이마다 머리 앞에서 쉬는 시간이다. 3 MiB를 512 KiB로 받으면 여섯 번이라 진행이 눈에 보인다.
        constexpr std::chrono::milliseconds update_chunk_delay { 300 };

        // 고정 봉투와 asset이다. 처리기가 한 번 지어 든다 (요청마다 3 MiB를 다시 짓지 않는다).
        struct update_assets
        {
            std::string envelope {};
            std::string broken_envelope {};
            std::vector<std::uint8_t> asset {};
            std::vector<std::uint8_t> tampered {};
        };

        [[nodiscard]] std::string ascii(const std::u8string_view text)
        {
            return std::string { reinterpret_cast<const char*>(text.data()), text.size() };
        }

        [[nodiscard]] std::shared_ptr<const update_assets> make_update_assets()
        {
            auto assets { std::make_shared<update_assets>() };
            assets->envelope = ascii(luil::testing::update_fixture_envelope(true));
            assets->broken_envelope = ascii(luil::testing::update_fixture_broken_signature_envelope());
            assets->asset = luil::testing::update_fixture_asset();
            assets->tampered = assets->asset;
            // 가운데 한 바이트다. 크기는 그대로라 끝의 해시만 잡는다.
            assets->tampered[assets->tampered.size() / 2] ^= 0x01;
            return assets;
        }

        // `Range: bytes=a-b`에 206으로, 없거나 무시하라면 200으로 답한다.
        [[nodiscard]] luil::testing::loopback_response make_update_asset_response(
            const luil::testing::loopback_request& request, const std::vector<std::uint8_t>& asset, const bool ignore_range)
        {
            luil::testing::loopback_response response {};
            response.headers.push_back({ "Content-Type", "application/octet-stream" });
            response.delay_before_headers = update_chunk_delay;
            std::uint64_t first { 0 };
            std::uint64_t last { asset.size() - 1 };
            const std::string range { request.header("range").value_or(std::string {}) };
            constexpr std::string_view prefix { "bytes=" };
            const std::size_t dash { range.find('-') };
            if (ignore_range == false && range.starts_with(prefix) && dash != std::string::npos)
            {
                const auto [first_end, first_code] { std::from_chars(range.data() + prefix.size(), range.data() + dash, first) };
                const auto [last_end, last_code] { std::from_chars(range.data() + dash + 1, range.data() + range.size(), last) };
                if (first_code != std::errc {} || last_code != std::errc {} || first >= asset.size() || last < first)
                {
                    response.status = 416;
                    return response;
                }
                last = std::min<std::uint64_t>(last, asset.size() - 1);
                response.status = 206;
                response.headers.push_back({ "Content-Range", "bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" + std::to_string(asset.size()) });
            }
            response.body.assign(asset.begin() + static_cast<std::ptrdiff_t>(first), asset.begin() + static_cast<std::ptrdiff_t>(last + 1));
            return response;
        }

        [[nodiscard]] std::u8string_view update_failure_name(const luil::update_failure failure) noexcept
        {
            switch (failure)
            {
            case luil::update_failure::none:
                return u8"none";
            case luil::update_failure::network:
                return u8"network";
            case luil::update_failure::http_status:
                return u8"http_status";
            case luil::update_failure::manifest_invalid:
                return u8"manifest_invalid";
            case luil::update_failure::signature_invalid:
                return u8"signature_invalid";
            case luil::update_failure::manifest_mismatch:
                return u8"manifest_mismatch";
            case luil::update_failure::replayed:
                return u8"replayed";
            case luil::update_failure::no_asset:
                return u8"no_asset";
            case luil::update_failure::too_large:
                return u8"too_large";
            case luil::update_failure::range_unsupported:
                return u8"range_unsupported";
            case luil::update_failure::hash_mismatch:
                return u8"hash_mismatch";
            case luil::update_failure::disk:
                return u8"disk";
            case luil::update_failure::busy:
                return u8"busy";
            case luil::update_failure::not_writable:
                return u8"not_writable";
            case luil::update_failure::launch_failed:
                return u8"launch_failed";
            case luil::update_failure::unsupported:
                return u8"unsupported";
            case luil::update_failure::cancelled:
                return u8"cancelled";
            case luil::update_failure::stopped:
                return u8"stopped";
            }
            return u8"?";
        }

        // MiB를 소수 한 자리로 적는다 ("1.5"). 반올림한다.
        [[nodiscard]] std::u8string mib_text(const std::uint64_t bytes)
        {
            constexpr std::uint64_t mib { 1024u * 1024u };
            const std::uint64_t tenths { (bytes * 10u + mib / 2u) / mib };
            return to_u8_number(tenths / 10u) + u8"." + to_u8_number(tenths % 10u);
        }

        // "한글 텍스트"를 euc-kr(cp949)로 손수 적은 바이트다.
        // 소스가 UTF-8이라 이 글자들을 그냥 적으면 UTF-8로 나간다 — 다른 charset이
        // 실제로 읽히는 것을 보이려면 바이트를 손으로 적는 수밖에 없다.
        constexpr std::array<std::uint8_t, 11> euc_kr_sample { 0xC7, 0xD1, 0xB1, 0xDB, 0x20, 0xC5, 0xD8, 0xBD, 0xBA, 0xC6, 0xAE };
    }

    std::function<luil::testing::loopback_response(const luil::testing::loopback_request&)> make_network_handler(std::shared_ptr<const network_update_serve_flags> flags)
    {
        // 그림은 **한 번** 읽어 handler가 든다. 요청마다 디스크를 두드리면
        // 되돌이 서버가 재는 것이 우리 파일 시스템이 된다.
        const auto gradient { std::make_shared<const std::vector<std::uint8_t>>(read_asset(L"gradient.png")) };
        const auto spinner { std::make_shared<const std::vector<std::uint8_t>>(read_asset(L"spinner.gif")) };
        // 회차는 서버 thread에서 오르므로 원자적으로 센다.
        const auto rounds { std::make_shared<std::atomic<std::uint64_t>>(0) };
        const std::shared_ptr<const update_assets> update { make_update_assets() };
        const std::string asset_path { "/update/" + ascii(luil::testing::update_fixture_asset_name) };

        return [gradient, spinner, rounds, update, asset_path, flags](const luil::testing::loopback_request& request) -> luil::testing::loopback_response {
            const std::size_t query { request.target.find('?') };
            const std::string path { request.target.substr(0, query == std::string::npos ? request.target.size() : query) };

            if (path == "/status")
                return make_text_response("application/json", R"({"name":"luil","pages":10,"ok":true})");
            if (path == "/image.png")
            {
                if (gradient->empty())
                    return make_missing_response(R"({"title":"assets/gradient.png is missing"})");
                return make_body_response("image/png", *gradient);
            }
            if (path == "/spinner.gif")
            {
                if (spinner->empty())
                    return make_missing_response(R"({"title":"assets/spinner.gif is missing"})");
                return make_body_response("image/gif", *spinner);
            }
            if (path == "/text")
                return make_text_response("text/plain; charset=utf-8", "네트워크에서 온 글이다.\n두 번째 줄.\n세 번째 줄 — UTF-8 그대로 읽힌다.\n");
            if (path == "/euc-kr")
                return make_body_response("text/plain; charset=euc-kr", std::vector<std::uint8_t> { euc_kr_sample.begin(), euc_kr_sample.end() });
            if (path == "/page")
            {
                // `?tab=notes`는 같은 문서의 다른 갈래다 — 새 창 요청을 이 웹뷰에서
                // 열었을 때 어디로 갔는지가 제목으로 보이게 한다.
                const bool notes { request.target.find("tab=notes") != std::string::npos };
                std::string html {
                    "<!doctype html><title>luil</title>"
                    "<style>body{font-family:system-ui;margin:24px}"
                    ".tail{margin-top:1400px;background:#ffcc00;padding:16px;font-weight:700}</style>",
                };
                if (notes)
                    html += "<h1>메모</h1><p>새 창 요청이 이 웹뷰에서 열린 갈래다.</p>";
                else
                    html += "<h1>webview의 자리</h1><p>라이브러리는 이 문서를 풀지 않는다. 웹뷰가 푼다.</p>";
                html +=
                    // 배율 자다. CSS 100px 상자의 **물리** 폭이 곧 래스터 배율이다 —
                    // 창의 배율이 바뀐 뒤 이 폭이 따라 바뀌면 웹뷰가 우리 배율을
                    // 따른 것이다 (webview-composition-design.md).
                    "<div style=\"width:100px;height:24px;background:#ff00ff\"></div>"
                    "<p>위 띠는 CSS 100px이다. 물리 폭이 배율이다.</p>"
                    // 정책이 막는 것들이다. 눌러 보면 페이지 아래 상태 줄에 무엇이
                    // 막혔는지 찍힌다 (webview-composition-design.md).
                    "<p><a href=\"/page?tab=notes\" target=\"_blank\">새 창으로 메모 열기 (target=_blank)</a> · "
                    "<a href=\"/download\">내려받기 (attachment)</a> · "
                    "<a href=\"about:blank\">about:blank (스킴 밖)</a></p>"
                    // 권한이다. 127.0.0.1은 보안 문맥이라 위치를 실제로 묻는다.
                    "<p id=\"perm\">위치 권한: 묻는 중</p>"
                    "<script>navigator.geolocation.getCurrentPosition("
                    "function(){document.getElementById('perm').textContent='위치 권한: 허용됨'},"
                    "function(e){document.getElementById('perm').textContent='위치 권한: 거부됨 (code '+e.code+')'})</script>"
                    // web message다. 열리면 하나 보내고, 단추 둘은 상한을 건드린다 —
                    // 초당 건수(300개를 한꺼번에)와 크기(상한 256 KiB를 넘는 하나).
                    "<p><button id=\"flood\">메시지 300개 쏟기</button> <button id=\"big\">300 KiB 메시지</button> "
                    "<span id=\"sent\"></span></p>"
                    "<script>var w=window.chrome&&window.chrome.webview;"
                    "if(w){w.postMessage({hello:'luil',dpr:devicePixelRatio,tab:" + std::string { notes ? "'notes'" : "'doc'" } + "});"
                    "document.getElementById('flood').onclick=function(){for(var i=0;i<300;i++)w.postMessage({flood:i});"
                    "document.getElementById('sent').textContent='300개 보냈다'};"
                    "document.getElementById('big').onclick=function(){w.postMessage({big:'x'.repeat(300*1024)});"
                    "document.getElementById('sent').textContent='300 KiB 보냈다'}}</script>"
                    // 휠 중계를 눈으로 확인할 수 있게 문서를 길게 둔다 —
                    // 중계하지 않으면 이 띠에 영영 닿지 못한다.
                    "<div class=tail>여기까지 굴러왔다면 휠이 중계된 것이다.</div>";
                return make_text_response("text/html; charset=utf-8", html);
            }
            if (path == "/download")
            {
                // 내려받기 갈래다. 라이브러리가 막으므로 이 몸은 디스크에 닿지 않아야 한다.
                luil::testing::loopback_response response { make_text_response("text/plain; charset=utf-8", "이 파일은 내려받히지 않아야 한다.\n") };
                response.headers.push_back({ "Content-Disposition", "attachment; filename=\"luil.txt\"" });
                return response;
            }
            if (path == "/octet")
            {
                // **Content-Type을 적지 않는다.** 냄새로 그림이 되는 자리다.
                if (gradient->empty())
                    return make_missing_response(R"({"title":"assets/gradient.png is missing"})");
                return make_body_response("", *gradient);
            }
            if (path == "/slow")
            {
                luil::testing::loopback_response response { make_text_response("application/json", R"({"slept_ms":3000})") };
                response.delay_before_headers = std::chrono::milliseconds { 3000 };
                return response;
            }
            if (path == "/missing")
                return make_missing_response(R"({"type":"about:blank","title":"Not Found","detail":"이 길은 없다"})");
            // 자동 업데이트 구역이다. 봉투는 미리 서명한 것이고, 확인란이 변조를 시킨다.
            if (path == network_update_manifest_path)
            {
                const bool broken { flags != nullptr && flags->break_signature.load() };
                return make_text_response("application/json", broken ? update->broken_envelope : update->envelope);
            }
            if (path == asset_path)
            {
                const bool tamper { flags != nullptr && flags->tamper_asset.load() };
                const bool ignore_range { flags != nullptr && flags->ignore_range.load() };
                return make_update_asset_response(request, tamper ? update->tampered : update->asset, ignore_range);
            }
            if (path == "/beat")
            {
                const std::uint64_t round { rounds->fetch_add(1) + 1 };
                return make_text_response("application/json", R"({"round":)" + std::to_string(round) + "}");
            }
            if (path == "/echo")
            {
                if (request.method != "POST")
                    return make_missing_response(R"({"title":"POST only"})");
                nlohmann::json body {};
                body["received"] = request.body.size();
                body["text"] = std::string { reinterpret_cast<const char*>(request.body.data()), request.body.size() };
                // 던지는 갈래를 쓰지 않는다 — 밖에서 온 바이트가 UTF-8이 아닐 수 있고,
                // 서버 thread를 벗어난 예외는 그 연결 하나가 아니라 예제 전체의 문제다.
                return make_text_response("application/json", body.dump(2, ' ', false, nlohmann::json::error_handler_t::replace));
            }
            return make_missing_response(R"({"title":"no such route"})");
        };
    }

    bool network_page::handle(const luil::app_message& message)
    {
        if (const auto* const response { message.get<network_response_intent>() }; response != nullptr)
        {
            take_response(response->response);
            return true;
        }
        if (const auto* const edit { message.get<edit_intent>() }; edit != nullptr)
        {
            // 편집 메시지는 target으로 나눠 갖는다 (기본 페이지·창 페이지와 같다).
            if (edit->request.target != target_network_url)
                return false;
            apply_edit(edit->request);
            return true;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            if (composition->event.target != target_network_url)
                return false;
            apply_composition(composition->event);
            return true;
        }
        if (const auto* const strip { message.get<network_strip_scroll_intent>() }; strip != nullptr)
        {
            strip_scroll_ += strip->delta;
            if (strip_scroll_ < 0.0f)
                strip_scroll_ = 0.0f;
            return true;
        }
        if (const auto* const preview { message.get<network_preview_scroll_intent>() }; preview != nullptr)
        {
            preview_scroll_ += preview->delta;
            if (preview_scroll_ < 0.0f)
                preview_scroll_ = 0.0f;
            return true;
        }
        if (const auto* const update { message.get<network_update_status_intent>() }; update != nullptr)
        {
            update_ = update->status;
            update_note_.clear();
            // 받을 판은 확인이 내준 뒤부터 다음 확인 전까지 있다. 받다 실패해도 그대로라 다시 받으면 잇는다.
            if (update_.phase == luil::update_phase::checking)
                update_offered_ = false;
            if (update_.phase == luil::update_phase::available || update_.phase == luil::update_phase::ready)
                update_offered_ = true;
            return true;
        }
        if (const auto* const flag { message.get<network_update_flag_intent>() }; flag != nullptr)
        {
            switch (flag->flag)
            {
            case network_update_flag::tamper_asset:
                serve_flags_->tamper_asset.store(flag->on);
                break;
            case network_update_flag::break_signature:
                serve_flags_->break_signature.store(flag->on);
                break;
            case network_update_flag::ignore_range:
                serve_flags_->ignore_range.store(flag->on);
                break;
            }
            return true;
        }
        if (const auto* const preview_to { message.get<network_preview_scroll_to_intent>() }; preview_to != nullptr)
        {
            // 절대 자리는 그대로 받는다. 위쪽 한계만 다듬고 아래쪽은 영역의
            // `arrange`가 같은 식으로 다듬는다 — 창 높이를 아는 것이 그쪽뿐이다.
            preview_scroll_ = preview_to->offset > 0.0f ? preview_to->offset : 0.0f;
            return true;
        }
        return false;
    }

    void network_page::set_base_url(std::u8string url)
    {
        base_url_ = std::move(url);
        if (base_url_.empty() == false && url_.text.empty())
            replace_url(base_url_ + u8"/status");
    }

    std::u8string network_page::heartbeat_url() const
    {
        return base_url_.empty() ? std::u8string {} : base_url_ + u8"/beat";
    }

    void network_page::set_in_flight(const std::size_t count) noexcept
    {
        in_flight_ = count;
    }

    std::shared_ptr<const network_update_serve_flags> network_page::update_serve_flags() const
    {
        return serve_flags_;
    }

    void network_page::set_updater(const bool ready, std::u8string error)
    {
        updater_ready_ = ready;
        update_note_ = std::move(error);
    }

    void network_page::note_update_refused(std::u8string error)
    {
        update_note_ = std::move(error);
    }

    void network_page::note_update_cleared(const bool ok, std::u8string error)
    {
        if (ok)
        {
            // 폴더를 비웠으니 받아 둔 것도 기록도 없다. 처음 상태로 돌아간다.
            update_ = luil::update_status {};
            update_offered_ = false;
            update_note_ = u8"받은 파일을 지웠다.";
            return;
        }
        update_note_ = std::move(error);
    }

    bool network_page::update_busy() const noexcept
    {
        return update_.phase == luil::update_phase::checking || update_.phase == luil::update_phase::downloading || update_.phase == luil::update_phase::verifying
            || update_.phase == luil::update_phase::applying;
    }

    std::optional<luil::taskbar_progress> network_page::update_taskbar_progress() const
    {
        // 라이브러리는 작업 표시줄을 만지지 않는다. 앱이 단계를 진행률로 옮긴다 (auto-update-design.md의 진행 표시).
        switch (update_.phase)
        {
        case luil::update_phase::downloading:
        {
            const float value { update_.total == 0 ? 0.0f : static_cast<float>(static_cast<double>(update_.received) / static_cast<double>(update_.total)) };
            return luil::taskbar_progress { luil::taskbar_progress_state::normal, value };
        }
        case luil::update_phase::checking:
        case luil::update_phase::verifying:
            return luil::taskbar_progress { luil::taskbar_progress_state::indeterminate, 0.0f };
        case luil::update_phase::failed:
            return luil::taskbar_progress { luil::taskbar_progress_state::error, 1.0f };
        case luil::update_phase::idle:
        case luil::update_phase::up_to_date:
        case luil::update_phase::available:
        case luil::update_phase::ready:
        case luil::update_phase::applying:
        case luil::update_phase::handed_off:
            break;
        }
        return std::nullopt;
    }

    std::u8string network_page::update_status_text() const
    {
        std::u8string text { u8"업데이트 상태: " };
        switch (update_.phase)
        {
        case luil::update_phase::idle:
            return text + u8"대기";
        case luil::update_phase::checking:
            return text + u8"확인 중";
        case luil::update_phase::up_to_date:
            return text + u8"최신 판";
        case luil::update_phase::available:
            if (update_.offer.has_value() == false)
                return text + u8"새 판";
            return text + u8"새 판 " + update_.offer->version + u8" · " + mib_text(update_.offer->size) + u8" MiB";
        case luil::update_phase::downloading:
            return text + u8"받는 중 " + mib_text(update_.received) + u8"/" + mib_text(update_.total) + u8" MiB";
        case luil::update_phase::verifying:
            return text + u8"검증 중";
        case luil::update_phase::ready:
            return text + u8"준비됨";
        case luil::update_phase::applying:
            return text + u8"적용 중";
        case luil::update_phase::handed_off:
            return text + u8"넘겼다";
        case luil::update_phase::failed:
            return text + u8"실패 (" + std::u8string { update_failure_name(update_.failure) } + u8")";
        }
        return text;
    }

    void network_page::note_sent(const network_send_intent& request, const luil::net::http_ticket ticket, const std::u8string& error)
    {
        request_line_ = std::u8string { request.post ? u8"POST " : u8"GET " } + path_of(request.url);
        replace_url(request.url);
        if (ticket)
        {
            pending_ = ticket;
            send_error_.clear();
            return;
        }
        // 손잡이가 서기 전의 실패다 — 답이 오지 않으므로 이 줄이 결말이다
        // (http_client.h가 가른 두 실패 자리 중 앞엣것이다).
        pending_ = {};
        send_error_ = error;
    }

    void network_page::note_heartbeat(const luil::net::http_ticket ticket, const std::u8string& error)
    {
        if (ticket)
        {
            heartbeat_ = ticket;
            heartbeat_on_ = true;
            heartbeat_error_.clear();
            beat_round_ = 0;
            beat_elapsed_ = std::chrono::milliseconds { 0 };
            beat_status_ = 0;
            return;
        }
        heartbeat_ = {};
        heartbeat_on_ = false;
        heartbeat_error_ = error;
    }

    void network_page::note_heartbeat_stopped() noexcept
    {
        heartbeat_ = {};
        heartbeat_on_ = false;
    }

    void network_page::apply_edit(const luil::text_edit_request& request)
    {
        luil::apply_text_edit(url_, request);
    }

    void network_page::apply_composition(const luil::text_composition_event& event)
    {
        // 조합이 끝나면 표시 상태를 버린다 — 확정된 글은 편집 요청으로 이미 들어왔다.
        if (event.composing == false)
        {
            composition_.reset();
            return;
        }
        composition_ = event;
    }

    void network_page::replace_url(const std::u8string& url)
    {
        if (url_.text == url)
            return;
        // 모두 고르고 넣기 — 편집 명령 둘로 적어 실행 취소가 그대로 성립한다
        // (노트 칸에 파일을 놓는 것과 같은 자리다).
        luil::text_edit_request request {};
        request.target = target_network_url;
        request.command = luil::text::text_edit_command::select_all;
        luil::apply_text_edit(url_, request);
        request.command = luil::text::text_edit_command::insert;
        request.text = url;
        luil::apply_text_edit(url_, request);
    }

    void network_page::take_response(const luil::net::http_response& response)
    {
        // **표가 흐름을 가른다** (http-client-design.md).
        // 되풀이의 표는 회차마다 바뀌지 않으므로 맥박은 이 한 줄로 알아본다.
        if (heartbeat_ && response.ticket == heartbeat_)
        {
            beat_round_ = response.sequence;
            beat_elapsed_ = response.elapsed;
            beat_status_ = response.status_code;
            // 더 오지 않는다고 답한 회차에서 표를 잊는다.
            if (response.last)
            {
                heartbeat_ = {};
                heartbeat_on_ = false;
            }
            return;
        }
        // 모르는 표는 지나간 흐름의 늦은 답이다 — 새 요청이 이미 그 자리를
        // 가져갔으므로 화면을 뒤집게 두지 않는다.
        if (pending_ == luil::net::http_ticket {} || response.ticket != pending_)
            return;

        pending_ = {};
        last_ = response;
        has_last_ = true;
        preview_lines_.clear();
        preview_scroll_ = 0.0f;
        playback_ = {};

        const luil::net::http_body& body { last_.body };
        switch (body.kind)
        {
        case luil::net::http_body_kind::json:
            if (body.json != nullptr)
                preview_lines_ = take_lines(from_utf8(body.json->dump(2, ' ', false, nlohmann::json::error_handler_t::replace)), json_preview_lines);
            else
                preview_lines_.push_back(u8"json으로 풀리지 않았다 — 아래 줄이 이유다.");
            break;
        case luil::net::http_body_kind::text:
            preview_lines_ = take_lines(body.as_text(), text_preview_lines);
            break;
        case luil::net::http_body_kind::image:
            // 값을 채우는 것이 곧 재생의 시작이다 (image_element.h).
            // 네트워크에서 온 gif가 여기서 그대로 돈다.
            if (body.image.valid())
                playback_.started = std::chrono::steady_clock::now();
            else
                preview_lines_.push_back(u8"그림을 풀지 못했다 — 아래 줄이 이유다.");
            break;
        case luil::net::http_body_kind::html:
            preview_lines_.push_back(u8"HTML은 지금 읽지 않는다 — webview의 자리다.");
            preview_lines_.push_back(
                u8"받아 둔 것: " + size_text(body.data().size()) + u8", charset " + (last_.content_type.charset.empty() ? std::u8string { u8"(없음)" } : last_.content_type.charset));
            break;
        case luil::net::http_body_kind::bytes:
            preview_lines_.push_back(u8"우리가 풀지 않는 바이트다 — " + size_text(body.data().size()) + u8". 실패가 아니라 정직한 답이다.");
            preview_lines_.push_back(u8"앞 16 byte: " + hex_preview(body.data()));
            break;
        case luil::net::http_body_kind::empty:
            preview_lines_.push_back(u8"몸이 없다.");
            break;
        }
    }

    std::u8string network_page::url_text() const
    {
        return url_.text;
    }

    std::u8string network_page::header_text() const
    {
        if (send_error_.empty() == false)
            return request_line_ + u8" · 보내지 못했다 — " + send_error_;
        if (pending_)
            return request_line_ + u8" · 보내는 중…";
        if (has_last_ == false)
            return u8"아직 보낸 것이 없다 — 과녁 단추를 누르거나 주소를 치고 보내기를 누른다.";

        std::u8string line { request_line_ };
        // 4xx·5xx는 오류가 아니라 답이다 — 상태 줄이 그대로 선다 (3.6).
        if (last_.error.empty())
            line += u8" · " + to_u8(last_.status_code) + (last_.reason.empty() ? std::u8string {} : u8" " + last_.reason);
        else
            line += u8" · " + std::u8string { error_kind_name(last_.error.kind) };
        line += u8" · " + to_u8_number(static_cast<std::uint64_t>(last_.elapsed.count())) + u8" ms";
        line += u8" · " + std::u8string { kind_name(last_.body.kind) } + u8" (" + std::u8string { kind_source_name(last_.body.kind_source) } + u8")";
        line += u8" · " + size_text(last_.body.data().size());
        return line;
    }

    std::u8string network_page::beat_text() const
    {
        std::u8string line {};
        if (heartbeat_error_.empty() == false)
            line = u8"켜지 못했다 — " + heartbeat_error_;
        else if (beat_round_ == 0)
            line = heartbeat_on_ ? std::u8string { u8"첫 회차를 기다린다" } : std::u8string { u8"꺼져 있다" };
        else
            line = u8"회차 " + to_u8_number(beat_round_) + u8" · 마지막 " + to_u8_number(static_cast<std::uint64_t>(beat_elapsed_.count())) + u8" ms · " + to_u8(beat_status_);
        return line + u8"    날아가 있는 요청 " + to_u8_number(static_cast<std::uint64_t>(in_flight_));
    }

    std::u8string network_page::note_text() const
    {
        if (has_last_ == false)
            return {};
        std::u8string note {};
        // 파싱 실패는 전송 오류가 아니다 — 둘이 한 줄에 함께 설 수 있다 (3.6).
        if (last_.body.parse_error.empty() == false)
            note = u8"parse_error: " + last_.body.parse_error;
        if (last_.error.empty() == false)
        {
            if (note.empty() == false)
                note += u8" · ";
            note += u8"error: " + last_.error.message;
        }
        return note;
    }

    std::unique_ptr<luil::ui_element> network_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(height);
        static_cast<void>(scale);

        luil::stack_config config {};
        config.padding = luil::edge_insets::all(24.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"network" }, config) };
        column->add(make_label(luil::ui_element_id { kind_text, u8"network-hint" }, u8"되돌이 서버가 이 프로세스 안에 서 있다 — 인터넷 없이 돈다. 주소를 고쳐 밖으로 보내도 된다.", 11.0f,
                        luil::label_color_role::dim),
            18.0f);
        column->add_gap(10.0f);
        column->add(make_address_row(), 28.0f);
        column->add_gap(8.0f);
        column->add(make_preset_strip(), 28.0f);
        column->add_gap(10.0f);
        column->add(make_control_row(), luil::check_row_height);
        column->add_gap(14.0f);
        column->add(make_update_section(), update_section_height);
        column->add_gap(14.0f);
        column->add(make_label(luil::ui_element_id { kind_text, u8"network-header" }, header_text(), 12.0f, luil::label_color_role::primary), 18.0f);
        column->add_gap(6.0f);
        column->add_flexible(make_preview());

        // 없는 줄은 두지 않는다 — 빈 자리를 잡으면 화면이 무엇을 말하는지 흐려진다.
        if (const std::u8string note { note_text() }; note.empty() == false)
        {
            column->add_gap(6.0f);
            column->add(make_label(luil::ui_element_id { kind_text, u8"network-note" }, note, 11.0f, luil::label_color_role::primary), 18.0f);
        }
        return column;
    }

    std::unique_ptr<luil::ui_element> network_page::make_address_row() const
    {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 8.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"network-address" }, config) };

        luil::text_input_view view { luil::make_text_input_view(url_, composition_, target_network_url) };
        luil::text_input_config input_config {};
        input_config.placeholder = u8"http://호스트/경로";
        auto input { std::make_unique<luil::text_input_element>(luil::ui_element_id { kind_network_url_input }, std::move(view), std::move(input_config)) };
        input->set_tooltip(u8"보낼 주소");
        row->add_flexible(std::move(input));

        auto send { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_network_send }, luil::text_button_config { .text = u8"보내기" }) };
        send->set_cursor(luil::ui_cursor::hand);
        send->set_tooltip(u8"주소 칸의 주소로 GET을 보낸다");
        send->set_enabled(url_text().empty() == false);
        send->set_action(luil::ui_trigger::left_click, luil::make_message_action(network_send_intent { url_text(), false }));
        row->add(std::move(send), 88.0f);
        return row;
    }

    std::unique_ptr<luil::ui_element> network_page::make_preset_strip() const
    {
        constexpr float spacing { 6.0f };
        luil::stack_config lane_config {};
        lane_config.direction = luil::stack_direction::row;
        lane_config.spacing = spacing;
        auto lane { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"network-presets" }, lane_config) };

        float content_width { 0.0f };
        for (const preset_target& target : preset_targets)
        {
            const std::u8string path { target.path };
            luil::text_button_config button_config {};
            button_config.text = std::u8string { target.label };
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_network_target, path }, std::move(button_config)) };
            button->set_cursor(luil::ui_cursor::hand);
            button->set_tooltip(std::u8string { target.post ? u8"POST " : u8"GET " } + path);
            // 서버가 서지 못했으면 과녁도 두지 않는다 — 눌러도 아무 일이 없는
            // 단추는 화면에서 고장으로 보인다.
            button->set_enabled(base_url_.empty() == false);
            if (base_url_.empty() == false)
                button->set_action(luil::ui_trigger::left_click, luil::make_message_action(network_send_intent { base_url_ + path, target.post }));
            const float button_width { label_width(target.label) };
            lane->add(std::move(button), button_width);
            content_width += button_width + spacing;
        }
        if (content_width > 0.0f)
            content_width -= spacing;

        // 좁은 창에서는 띠가 흘러 준다 — 넘치는 단추가 사라지지 않는다.
        luil::strip_config strip_config {};
        strip_config.content_width = content_width;
        strip_config.scroll_offset = strip_scroll_;
        // 손가락으로 옆으로 쓸어 흘린다. 휠 표와 같은 메시지다.
        strip_config.scroll = [](const float value) { return luil::make_app_action(network_strip_scroll_intent { value }); };
        auto strip { std::make_unique<luil::strip_element>(luil::ui_element_id { kind_network_preview, u8"strip" }, strip_config) };
        strip->set_content(std::move(lane));
        return strip;
    }

    std::unique_ptr<luil::ui_element> network_page::make_control_row() const
    {
        luil::stack_config config {};
        config.direction = luil::stack_direction::row;
        config.spacing = 12.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"network-controls" }, config) };

        // 확인란은 스스로 바뀌지 않는다 — **바뀐 뒤의 상태**를 실은 액션을 매
        // frame 새로 싣고, 실제로 켜고 끄는 것은 셸이 client에게 시킨다.
        luil::check_config beat {};
        beat.owner = u8"beat";
        beat.style = luil::check_style::checkbox;
        beat.label = u8"심장 박동 (1초)";
        beat.checked = heartbeat_on_;
        beat.tooltip = u8"/beat을 1초 쉬고 되풀이해 부른다";
        if (base_url_.empty() == false)
            beat.toggle = luil::make_message_action(network_heartbeat_intent { heartbeat_on_ == false });
        row->add(std::make_unique<luil::check_element>(std::move(beat)), 150.0f);

        row->add_flexible(make_label(luil::ui_element_id { kind_text, u8"network-beat" }, beat_text(), 11.0f, luil::label_color_role::dim));

        auto cancel { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_network_cancel }, luil::text_button_config { .text = u8"취소" }) };
        cancel->set_cursor(luil::ui_cursor::hand);
        cancel->set_tooltip(u8"보낸 요청을 접는다 (답은 그래도 온다)");
        // 겨눌 본문 흐름이 없으면 잠근다. `in_flight()`로 열면 맥박만 도는 동안에도
        // 열려, 누르는 것이 맥박을 접는 일이 된다 — 단추의 과녁은 본문 하나뿐이다.
        cancel->set_enabled(static_cast<bool>(pending_));
        cancel->set_action(luil::ui_trigger::left_click, luil::make_message_action(network_cancel_intent {}));
        row->add(std::move(cancel), 88.0f);
        return row;
    }

    std::unique_ptr<luil::ui_element> network_page::make_update_section() const
    {
        luil::stack_config config {};
        config.spacing = update_section_spacing;
        auto section { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"network-update" }, config) };
        section->add(make_label(luil::ui_element_id { kind_text, u8"network-update-title" }, u8"자동 업데이트", 12.0f, luil::label_color_role::primary), update_line_height);

        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 8.0f;
        auto buttons { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"network-update-buttons" }, row_config) };
        const bool busy { update_busy() };
        // 단추는 과녁 단추와 같은 kind를 owner로 가른다.
        const auto add_button {
            [&buttons](const std::u8string_view owner, const std::u8string_view label, const std::u8string_view tooltip, const bool enabled, luil::ui_action action) {
                auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_network_target, std::u8string { owner } }, luil::text_button_config { .text = std::u8string { label } }) };
                button->set_cursor(luil::ui_cursor::hand);
                button->set_tooltip(std::u8string { tooltip });
                button->set_enabled(enabled);
                if (enabled)
                    button->set_action(luil::ui_trigger::left_click, std::move(action));
                buttons->add(std::move(button), label_width(label));
            },
        };
        add_button(u8"update-check", u8"업데이트 확인", u8"되돌이 서버의 서명한 매니페스트를 받아 확인한다", updater_ready_ && busy == false,
            luil::make_message_action(network_update_check_intent {})
        );
        add_button(u8"update-download", u8"내려받기", u8"새 판을 512 KiB 덩이로 받고 SHA-256을 맞춘다", updater_ready_ && busy == false && update_offered_,
            luil::make_message_action(network_update_download_intent {})
        );
        add_button(u8"update-cancel", u8"취소", u8"하던 확인·받기를 접는다 (받던 파일은 남는다)", updater_ready_ && busy, luil::make_message_action(network_update_cancel_intent {}));
        add_button(u8"update-clear", u8"받은 파일 지우기", u8"%LOCALAPPDATA%\\luil-demo\\update 폴더를 비운다", updater_ready_ && busy == false,
            luil::make_message_action(network_update_clear_intent {})
        );
        section->add(std::move(buttons), 28.0f);

        auto checks { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"network-update-flags" }, row_config) };
        const auto add_check {
            [&checks](const std::u8string_view owner, const std::u8string_view label, const std::u8string_view tooltip, const network_update_flag flag, const bool on) {
                luil::check_config check {};
                check.owner = std::u8string { owner };
                check.style = luil::check_style::checkbox;
                check.label = std::u8string { label };
                check.checked = on;
                check.tooltip = std::u8string { tooltip };
                check.toggle = luil::make_message_action(network_update_flag_intent { flag, on == false });
                checks->add(std::make_unique<luil::check_element>(std::move(check)), label_width(label) + 24.0f);
            },
        };
        add_check(u8"update-tamper", u8"변조된 파일 내기", u8"서버가 asset 한 바이트를 바꿔 낸다", network_update_flag::tamper_asset, serve_flags_->tamper_asset.load());
        add_check(u8"update-break", u8"서명 깨기", u8"서버가 봉투의 서명 한 바이트를 바꿔 낸다", network_update_flag::break_signature, serve_flags_->break_signature.load());
        add_check(u8"update-range", u8"Range 무시", u8"서버가 Range를 무시하고 파일 전체를 200으로 낸다", network_update_flag::ignore_range, serve_flags_->ignore_range.load());
        section->add(std::move(checks), luil::check_row_height);

        section->add(make_label(luil::ui_element_id { kind_text, u8"network-update-status" }, update_status_text(), 12.0f, luil::label_color_role::primary), update_line_height);
        std::u8string note { update_note_.empty() ? update_.error : update_note_ };
        if (note.empty())
            note = u8"받는 자리: %LOCALAPPDATA%\\luil-demo\\update";
        section->add(make_label(luil::ui_element_id { kind_text, u8"network-update-note" }, u8"업데이트 진단: " + note, 11.0f, luil::label_color_role::dim), update_line_height);
        // 적용은 시연하지 않는다. 돌고 있는 데모의 실행 파일을 바꾸지 않기 위해서다. 적용 흐름은 update_apply_tests의 시험 앱이 돈다.
        const std::u8string apply_mode { u8"업데이트 적용 방식: " + std::u8string { apply_mode_text(luil::platform_update_apply_mode()) } };
        section->add(make_label(luil::ui_element_id { kind_text, u8"network-update-apply-mode" }, apply_mode, 11.0f, luil::label_color_role::dim), update_line_height);
        return section;
    }

    std::unique_ptr<luil::ui_element> network_page::make_preview() const
    {
        luil::panel_config config {};
        config.corner_radius = 6.0f;
        config.background = [](const luil::ui_color_palette& palette) { return palette.input_background; };
        auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_network_preview, u8"panel" }, config) };

        luil::stack_config inner {};
        inner.padding = luil::edge_insets::all(12.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"network-preview" }, inner) };
        if (has_last_ && last_.body.kind == luil::net::http_body_kind::image && last_.body.image.valid())
        {
            // 움직이는 그림이면 그대로 돈다 — 파일에서 읽은 것과 같은 값이라
            // 재생 시계 하나면 된다 (image-anim-design.md).
            column->add_flexible(std::make_unique<luil::image_element>(luil::ui_element_id { kind_network_preview, u8"image" },
                luil::image_config { .fit = luil::image_fit::contain, .description = request_line_, .animation = last_.body.image, .playback = playback_ }));
        }
        else
            column->add_flexible(make_lines_view());
        panel->set_content(std::move(column));
        return panel;
    }

    std::unique_ptr<luil::ui_element> network_page::make_lines_view() const
    {
        // 흘리는 창 하나에 막대·치수·휠·되살리기가 딸려 온다.
        //
        // 손으로 조립하던 때 이 칸에는 **막대가 없었다.** 막대를 세우려면 창이 실제로
        // 받은 높이를 알아야 하는데 그 높이는 배치가 정해져야 나오고, 앱은 그것을
        // 추측할 수밖에 없었다 — 추측이 틀리면 thumb와 흘릴 수 있는 양이 어긋난다.
        // 여기 설정 하나가 그 다섯을 `arrange` 안으로 가져간다 (scroll-area-design.md).
        luil::scroll_area_config config {};
        config.owner = u8"network-preview";
        config.content_height = static_cast<float>(preview_lines_.size()) * preview_line_height;
        config.scroll_offset = preview_scroll_;
        // **이 하나가 넷을 함께 켠다** — 휠, 막대의 끌기와 키, 그리고 초점 되살리기다.
        config.scroll = [](const float delta) { return luil::make_app_action(network_preview_scroll_intent { delta }); };
        // 절대 자리는 따로 받는다. 델타로 환산해 보내면 오래된 발행본 기준의
        // 변화량이 겹쳐 쌓여 보조 기술이 겨눈 자리에 서지 못한다.
        config.scroll_to = [](const float offset) { return luil::make_app_action(network_preview_scroll_to_intent { offset }); };
        auto view { std::make_unique<luil::scroll_area_element>(std::move(config)) };
        // 막대는 영역의 이름을 물려받는다. 이름이 없으면 막대가 이름 없이 읽힌다.
        view->set_access_name(u8"응답 미리보기");

        luil::stack_config inner {};
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"network-lines" }, inner) };
        for (std::size_t index = 0; index < preview_lines_.size(); ++index)
            column->add(
                make_label(luil::ui_element_id { kind_text, u8"network-line-" + to_u8_number(static_cast<std::uint64_t>(index)) }, preview_lines_[index], 11.0f, luil::label_color_role::dim),
                preview_line_height);
        view->set_content(std::move(column));
        return view;
    }

    std::vector<luil::input_action> network_page::route_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, const float delta)
    {
        // 표에 남는 것은 가로로 흘리는 띠 하나다.
        // 결과 칸의 줄이 여기서 사라진 것이 요점이다 — 그 칸을 영역으로 바꾸자
        // 휠도 되살리기도 적을 것이 없어졌다.
        static const luil::scroll_route routes[] {
            { { kind_network_preview, u8"strip" }, [](const float value) { return luil::make_app_action(network_strip_scroll_intent { value }); } },
        };
        return luil::route_wheel(tree, event.x, event.y, delta, routes);
    }
}
