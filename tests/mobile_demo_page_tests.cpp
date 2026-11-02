#include "demo/code_lexer.h"
#include "demo/line_break_sample.h"
#include "mobile_demo/sheets_page.h"
#include "mobile_demo/smb_page.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
    template<typename Message, typename Page>
    void deliver(Page& page, Message message)
    {
        REQUIRE(page.handle(luil::app_message { std::move(message) }));
    }

    [[nodiscard]] luil::ui_tree sheet_tree(mobile_demo::sheets_page& page, const float width, const float height)
    {
        auto root { std::make_unique<luil::root_element>() };
        page.add_overlay(*root, width, height, 1.0f, {});
        return luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, width, height }, 1.0f);
    }

    // 요청을 기록하고 사람이 고른 차례로 답한다. 실제 페이지의 send·cancel·응답 처리 경로를 그대로 쓴다.
    struct smb_fixture
    {
        std::vector<luil::net::smb_request> requests {};
        std::vector<luil::net::smb_ticket> cancelled {};
        std::uint64_t next_ticket { 0 };
        int discoveries { 0 };
        bool reject { false };
        mobile_demo::smb_page page { transport() };

        [[nodiscard]] mobile_demo::smb_page_transport transport()
        {
            return {
                .send =
                    [this](luil::net::smb_request request, std::u8string& error) {
                        requests.push_back(std::move(request));
                        if (reject)
                        {
                            error = u8"busy";
                            return luil::net::smb_ticket {};
                        }
                        return luil::net::smb_ticket { ++next_ticket };
                    },
                .cancel = [this](const luil::net::smb_ticket ticket) { cancelled.push_back(ticket); },
                .discover =
                    [this](const luil::net::smb_discovery_request, std::u8string&) {
                        ++discoveries;
                        return luil::net::smb_ticket { ++next_ticket };
                    },
            };
        }

        smb_fixture()
        {
            address(u8"nas/share");
            deliver(page, mobile_demo::smb_open_intent {});
            luil::net::smb_response response {
                .ticket = { next_ticket },
                .entries = { { .name = u8"photos", .directory = true }, { .name = u8"old.txt" } },
            };
            deliver(page, mobile_demo::smb_result_intent { std::move(response) });
        }

        void address(std::u8string value)
        {
            deliver(page,
                mobile_demo::edit_intent { luil::text_edit_request { .target = mobile_demo::target_smb_address, .command = luil::text::text_edit_command::replace_all, .text = std::move(value) } });
        }

        [[nodiscard]] bool has_entry(const std::u8string& name)
        {
            auto content { page.build(400.0f, 1.0f) };
            const auto tree { luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, content.height }, 1.0f) };
            return tree.find({ mobile_demo::kind_smb_entry, u8"smb-entry-" + name }) != nullptr;
        }
    };
}

TEST_CASE("The demo clears an interrupted adaptive sheet drag while preserving its stop and list", "[ui][sheet][mobile-demo]")
{
    for (const auto detent : { luil::sheet_detent::half, luil::sheet_detent::full })
    {
        CAPTURE(detent);
        mobile_demo::sheets_page page {};
        deliver(page, mobile_demo::sheet_open_intent {});
        deliver(page, mobile_demo::sheet_settle_intent { detent });
        deliver(page, mobile_demo::sheet_scroll_intent { 150.0f });
        deliver(page, mobile_demo::sheet_drag_intent { 20.0f });
        const float height { detent == luil::sheet_detent::half ? 400.0f : 776.0f };
        const luil::ui_element_id id { luil::ui_element_kind::sheet_host, u8"demo-sheet" };
        auto portrait { sheet_tree(page, 400.0f, 800.0f) };
        REQUIRE(static_cast<const luil::sheet_host_element*>(portrait.find(id))->surface_bounds().height == height - 20.0f);
        const auto landscape { sheet_tree(page, 800.0f, 400.0f) };
        REQUIRE(static_cast<const luil::sheet_host_element*>(landscape.find(id))->kind() == luil::sheet_kind::end);
        portrait = sheet_tree(page, 400.0f, 800.0f);
        REQUIRE(page.sheet_open());
        REQUIRE(static_cast<const luil::sheet_host_element*>(portrait.find(id))->surface_bounds().height == height);
        const auto* const list { portrait.find({ mobile_demo::kind_page_scroll, u8"sheet-list" }) };
        REQUIRE(list != nullptr);
        REQUIRE(list->scroll()->at_start == false);
    }
}

TEST_CASE("SMB navigation removes stale rows while pending or failed and keeps the confirmed folder", "[smb][mobile-demo]")
{
    smb_fixture fixture {};
    REQUIRE(fixture.has_entry(u8"old.txt"));
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"photos", true });
    REQUIRE(fixture.requests.back().path == u8"photos");
    REQUIRE(fixture.has_entry(u8"old.txt") == false);
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"old.txt", false });
    deliver(fixture.page, mobile_demo::smb_up_intent {});
    REQUIRE(fixture.requests.size() == 2u);
    deliver(fixture.page, mobile_demo::smb_result_intent { luil::net::smb_response { .ticket = { fixture.next_ticket }, .error = { .kind = luil::net::smb_error_kind::access_denied } } });
    REQUIRE(fixture.has_entry(u8"old.txt") == false);
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"old.txt", false });
    REQUIRE(fixture.requests.size() == 2u);
    deliver(fixture.page, mobile_demo::smb_up_intent {});
    REQUIRE(fixture.requests.back().path.empty());

    // 두 단계 아래로 가다 실패해도 마지막으로 확인된 photos의 위는 공유 루트다.
    for (const bool reject : { false, true })
    {
        CAPTURE(reject);
        smb_fixture nested {};
        deliver(nested.page, mobile_demo::smb_entry_intent { u8"photos", true });
        deliver(nested.page, mobile_demo::smb_result_intent { luil::net::smb_response { .ticket = { nested.next_ticket }, .entries = { { .name = u8"albums", .directory = true } } } });
        nested.reject = reject;
        deliver(nested.page, mobile_demo::smb_entry_intent { u8"albums", true });
        REQUIRE(nested.requests.back().path == u8"photos/albums");
        if (reject == false)
            deliver(nested.page, mobile_demo::smb_result_intent { luil::net::smb_response { .ticket = { nested.next_ticket }, .error = { .kind = luil::net::smb_error_kind::access_denied } } });
        nested.reject = false;
        deliver(nested.page, mobile_demo::smb_up_intent {});
        REQUIRE(nested.requests.back().path.empty());
    }
}

TEST_CASE("SMB successful navigation binds the new rows to the new folder", "[smb][mobile-demo]")
{
    smb_fixture fixture {};
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"photos", true });
    deliver(fixture.page, mobile_demo::smb_result_intent { luil::net::smb_response { .ticket = { fixture.next_ticket }, .entries = { { .name = u8"new.txt" } } } });
    REQUIRE(fixture.has_entry(u8"new.txt"));
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"new.txt", false });
    REQUIRE(fixture.requests.back().operation == luil::net::smb_operation::read);
    REQUIRE(fixture.requests.back().path == u8"photos/new.txt");
    deliver(fixture.page, mobile_demo::smb_up_intent {});
    REQUIRE(fixture.requests.back().path.empty());
    REQUIRE(fixture.cancelled.back() == luil::net::smb_ticket { 3 });
}

TEST_CASE("SMB file reading continues range by range until the end", "[smb][mobile-demo]")
{
    smb_fixture fixture {};
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"old.txt", false });
    REQUIRE(fixture.requests.back().operation == luil::net::smb_operation::read);
    REQUIRE(fixture.requests.back().offset == 0u);
    const std::size_t preview { fixture.requests.back().length };
    luil::net::smb_response first { .ticket = { fixture.next_ticket }, .operation = luil::net::smb_operation::read };
    first.data.resize(preview);
    deliver(fixture.page, mobile_demo::smb_result_intent { std::move(first) });

    // 미리 보기가 꽉 찼다. 다음 구간은 그 끝에서 시작한다.
    deliver(fixture.page, mobile_demo::smb_next_range_intent {});
    REQUIRE(fixture.requests.back().path == u8"old.txt");
    REQUIRE(fixture.requests.back().offset == preview);
    const std::size_t range { fixture.requests.back().length };
    luil::net::smb_response last { .ticket = { fixture.next_ticket }, .operation = luil::net::smb_operation::read };
    last.data.resize(range / 2u);
    deliver(fixture.page, mobile_demo::smb_result_intent { std::move(last) });

    // 짧게 왔다 — 파일 끝이다. 더 보내지 않는다.
    const std::size_t sent { fixture.requests.size() };
    deliver(fixture.page, mobile_demo::smb_next_range_intent {});
    CHECK(fixture.requests.size() == sent);
}

TEST_CASE("SMB with only a server lists its visible shares and opens the chosen one", "[smb][mobile-demo]")
{
    smb_fixture fixture {};
    fixture.address(u8"nas");
    deliver(fixture.page, mobile_demo::smb_open_intent {});
    REQUIRE(fixture.requests.back().operation == luil::net::smb_operation::shares);
    REQUIRE(fixture.requests.back().location.server == u8"nas");
    luil::net::smb_response response { .ticket = { fixture.next_ticket }, .operation = luil::net::smb_operation::shares };
    using luil::net::smb_share_kind;
    response.shares = {
        { .name = u8"photos", .kind = smb_share_kind::disk, .comment = u8"Family" },
        { .name = u8"IPC$", .kind = smb_share_kind::ipc, .special = true },
        { .name = u8"C$", .kind = smb_share_kind::disk, .special = true },
        { .name = u8"backup$", .kind = smb_share_kind::disk },
        { .name = u8"printer", .kind = smb_share_kind::printer },
    };
    deliver(fixture.page, mobile_demo::smb_result_intent { std::move(response) });
    // 디스크이고 숨지 않은 공유만 보인다.
    auto content { fixture.page.build(400.0f, 1.0f) };
    const auto tree { luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, content.height }, 1.0f) };
    CHECK(tree.find({ mobile_demo::kind_smb_entry, u8"smb-share-photos" }) != nullptr);
    CHECK(tree.find({ mobile_demo::kind_smb_entry, u8"smb-share-C$" }) == nullptr);
    CHECK(tree.find({ mobile_demo::kind_smb_entry, u8"smb-share-backup$" }) == nullptr);
    CHECK(tree.find({ mobile_demo::kind_smb_entry, u8"smb-share-printer" }) == nullptr);

    // 목록에 없는 공유는 열지 않는다.
    const std::size_t sent { fixture.requests.size() };
    deliver(fixture.page, mobile_demo::smb_share_intent { u8"C$" });
    CHECK(fixture.requests.size() == sent);
    // 고른 공유는 주소를 바꾸고 그 맨 위를 연다.
    deliver(fixture.page, mobile_demo::smb_share_intent { u8"photos" });
    REQUIRE(fixture.requests.size() == sent + 1u);
    CHECK(fixture.requests.back().operation == luil::net::smb_operation::list);
    CHECK(fixture.requests.back().location.server == u8"nas");
    CHECK(fixture.requests.back().location.share == u8"photos");
    CHECK(fixture.requests.back().path.empty());
}

TEST_CASE("SMB discovery lists found servers and opens the share list of the chosen one", "[smb][mobile-demo]")
{
    smb_fixture fixture {};
    deliver(fixture.page, mobile_demo::smb_discover_intent {});
    REQUIRE(fixture.discoveries == 1);
    luil::net::smb_response found { .ticket = { fixture.next_ticket }, .operation = luil::net::smb_operation::discover };
    found.servers = { { .name = u8"Living Room NAS", .host = u8"nas.local", .addresses = { u8"192.168.0.10" }, .port = 445 } };
    deliver(fixture.page, mobile_demo::smb_result_intent { std::move(found) });
    auto content { fixture.page.build(400.0f, 1.0f) };
    const auto tree { luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, content.height }, 1.0f) };
    CHECK(tree.find({ mobile_demo::kind_smb_entry, u8"smb-server-Living Room NAS" }) != nullptr);

    // 찾지 않은 서버는 열지 않는다.
    const std::size_t sent { fixture.requests.size() };
    deliver(fixture.page, mobile_demo::smb_server_intent { u8"10.0.0.9" });
    CHECK(fixture.requests.size() == sent);
    // 찾은 서버를 누르면 그 서버의 공유 목록을 묻는다.
    deliver(fixture.page, mobile_demo::smb_server_intent { u8"192.168.0.10" });
    REQUIRE(fixture.requests.size() == sent + 1u);
    CHECK(fixture.requests.back().operation == luil::net::smb_operation::shares);
    CHECK(fixture.requests.back().location.server == u8"192.168.0.10");
}

TEST_CASE("SMB rejected and replaced requests cannot revive an old listing", "[smb][mobile-demo]")
{
    smb_fixture fixture {};
    fixture.reject = true;
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"photos", true });
    REQUIRE(fixture.has_entry(u8"old.txt") == false);
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"old.txt", false });
    REQUIRE(fixture.requests.size() == 2u);
    fixture.reject = false;
    deliver(fixture.page, mobile_demo::smb_open_intent {});
    const auto replaced { fixture.next_ticket };
    deliver(fixture.page, mobile_demo::smb_open_intent {});
    REQUIRE(fixture.cancelled.back() == luil::net::smb_ticket { replaced });
    deliver(fixture.page, mobile_demo::smb_result_intent { luil::net::smb_response { .ticket = { replaced }, .entries = { { .name = u8"late.txt" } } } });
    REQUIRE(fixture.has_entry(u8"late.txt") == false);
    const auto edited { fixture.next_ticket };
    fixture.address(u8"other/share");
    REQUIRE(fixture.cancelled.back() == luil::net::smb_ticket { edited });
    deliver(fixture.page, mobile_demo::smb_result_intent { luil::net::smb_response { .ticket = { edited }, .entries = { { .name = u8"late.txt" } } } });
    REQUIRE(fixture.has_entry(u8"late.txt") == false);
    deliver(fixture.page, mobile_demo::smb_entry_intent { u8"late.txt", false });
    REQUIRE(fixture.requests.size() == 4u);
}

TEST_CASE("The demo code lexer paints six roles with bold keywords and italic comments", "[demo][syntax]")
{
    luil::syntax_highlighter highlighter { demo::code_highlighter_config() };
    // 둘째 줄이 열고 셋째 줄이 닫는 여러 줄 주석이 든다.
    const std::u8string text { u8"if (a) return f(10); // end\nint n = \"xy\"; /* a\n still */ 7" };
    highlighter.update(text);
    const auto spans { highlighter.spans() };
    REQUIRE(spans->size() == 10u);
    const auto role_of = [&](const std::size_t index) { return (*spans)[index].color; };
    const auto begin_of = [&](const std::size_t index) { return (*spans)[index].begin; };
    const auto end_of = [&](const std::size_t index) { return (*spans)[index].end; };
    // 키워드는 굵게다.
    REQUIRE(role_of(0) == luil::text_color_role::keyword);
    REQUIRE((*spans)[0].bold);
    REQUIRE((begin_of(0) == 0u && end_of(0) == 2u));
    REQUIRE(role_of(1) == luil::text_color_role::keyword);
    REQUIRE((begin_of(1) == 7u && end_of(1) == 13u));
    // 호출되는 이름은 함수, 숫자는 숫자다.
    REQUIRE(role_of(2) == luil::text_color_role::function);
    REQUIRE((begin_of(2) == 14u && end_of(2) == 15u));
    REQUIRE(role_of(3) == luil::text_color_role::number);
    REQUIRE((begin_of(3) == 16u && end_of(3) == 18u));
    // 주석은 기울임이고 줄 바꿈 바이트를 덮지 않는다. 여러 줄 주석은 줄마다 구간이다.
    for (const std::size_t index : { std::size_t { 4 }, std::size_t { 7 }, std::size_t { 8 } })
    {
        REQUIRE(role_of(index) == luil::text_color_role::comment);
        REQUIRE((*spans)[index].italic);
        REQUIRE_FALSE((*spans)[index].bold);
    }
    REQUIRE((begin_of(4) == 21u && end_of(4) == 27u));
    REQUIRE(role_of(5) == luil::text_color_role::type);
    REQUIRE((begin_of(5) == 28u && end_of(5) == 31u));
    REQUIRE(role_of(6) == luil::text_color_role::string);
    REQUIRE((begin_of(6) == 36u && end_of(6) == 40u));
    REQUIRE((begin_of(7) == 42u && end_of(7) == 46u));
    REQUIRE((begin_of(8) == 47u && end_of(8) == 56u));
    REQUIRE(role_of(9) == luil::text_color_role::number);
    REQUIRE((begin_of(9) == 57u && end_of(9) == 58u));

    const demo::code_span_counts counts { demo::count_code_spans(*spans) };
    REQUIRE(counts.total == 10u);
    REQUIRE(counts.keywords == 2u);
    REQUIRE(counts.strings == 1u);
    REQUIRE(counts.comments == 3u);
    REQUIRE(demo::code_status_text(counts, highlighter.stats().relexed) == u8"구간 10 · 키워드 2 · 문자열 1 · 주석 3 · 마지막에 다시 칠한 줄 3");
}

TEST_CASE("The demo code lexer reads escapes in a string and an unclosed string ends at the line", "[demo][syntax]")
{
    luil::syntax_highlighter highlighter { demo::code_highlighter_config() };
    const std::u8string text { u8"a \"b\\\"c\" d\ne 'f\ng 1.5e3 h" };
    highlighter.update(text);
    const auto spans { highlighter.spans() };
    REQUIRE(spans->size() == 3u);
    // 역슬래시 뒤의 따옴표는 문자열을 닫지 않는다.
    REQUIRE((*spans)[0].color == luil::text_color_role::string);
    REQUIRE((*spans)[0].begin == 2u);
    REQUIRE((*spans)[0].end == 8u);
    // 닫히지 않은 따옴표는 줄 끝까지이고 다음 줄로 이어지지 않는다.
    REQUIRE((*spans)[1].color == luil::text_color_role::string);
    REQUIRE((*spans)[1].begin == 13u);
    REQUIRE((*spans)[1].end == 15u);
    // 소수와 지수가 든 숫자는 한 구간이다.
    REQUIRE((*spans)[2].color == luil::text_color_role::number);
    REQUIRE((*spans)[2].begin == 18u);
    REQUIRE((*spans)[2].end == 23u);
}

TEST_CASE("The code sample shows a one-line edit relexing one line and a comment toggling the counts", "[demo][syntax]")
{
    luil::syntax_highlighter highlighter { demo::code_highlighter_config() };
    const std::u8string sample { demo::code_sample };
    highlighter.update(sample);
    const demo::code_span_counts original { demo::count_code_spans(*highlighter.spans()) };
    const std::vector<luil::text_span> original_spans { *highlighter.spans() };
    REQUIRE(original.total == 22u);
    REQUIRE(original.keywords == 4u);
    REQUIRE(original.strings == 1u);
    REQUIRE(original.comments == 4u);
    // 처음 불러올 때는 14줄(마지막 빈 줄 포함) 모두를 렉서에 보낸다. 실행한 데모의 상태 줄이 처음에 이 글을 그린다.
    REQUIRE(demo::code_status_text(original, highlighter.stats().relexed) == u8"구간 22 · 키워드 4 · 문자열 1 · 주석 4 · 마지막에 다시 칠한 줄 14");

    // 한 줄 편집이다. 숫자 하나를 바꾸면 렉서에 가는 줄은 하나이고 구간 수는 그대로다.
    std::u8string edited { sample };
    edited.replace(edited.find(u8"total = 0;") + 8, 1, u8"9");
    highlighter.update(edited);
    REQUIRE(highlighter.stats().relexed == 1u);
    REQUIRE(demo::code_status_text(demo::count_code_spans(*highlighter.spans()), highlighter.stats().relexed) == u8"구간 22 · 키워드 4 · 문자열 1 · 주석 4 · 마지막에 다시 칠한 줄 1");

    // `int total` 줄 앞에서 `/*`를 열면 끝까지 주석이 되어 주석 수가 늘고 키워드는 줄어든다.
    std::u8string opened { sample };
    opened.insert(opened.find(u8"    int total"), u8"/*");
    highlighter.update(opened);
    const demo::code_span_counts commented { demo::count_code_spans(*highlighter.spans()) };
    REQUIRE(highlighter.stats().relexed == 9u);
    REQUIRE(commented.comments > original.comments);
    REQUIRE(commented.keywords < original.keywords);
    REQUIRE(demo::code_status_text(commented, highlighter.stats().relexed) == u8"구간 15 · 키워드 1 · 문자열 0 · 주석 10 · 마지막에 다시 칠한 줄 9");

    // 지우면 처음 그림으로 돌아온다.
    highlighter.update(sample);
    const demo::code_span_counts restored { demo::count_code_spans(*highlighter.spans()) };
    REQUIRE(restored.total == original.total);
    REQUIRE(restored.comments == original.comments);
    REQUIRE(restored.keywords == original.keywords);
    REQUIRE(demo::code_status_text(restored, highlighter.stats().relexed) == u8"구간 22 · 키워드 4 · 문자열 1 · 주석 4 · 마지막에 다시 칠한 줄 9");
    REQUIRE(highlighter.spans()->size() == original_spans.size());
}

TEST_CASE("The demo line break cell counts lines with both policies", "[demo][line-break]")
{
    // ASCII는 글자 크기의 0.6배, 그 밖(한글·한자·가나)은 글자 크기만큼의 폭이다. 실제 글꼴과 다르므로 화면의 값은
    // 검증 때 다시 읽는다.
    const luil::text_measurer measurer { [](const std::u8string_view text, const float size) {
        float width { 0.0f };
        for (std::size_t offset { 0 }; offset < text.size();)
        {
            const auto decoded { luil::text::utf8_decode(text, offset) };
            width += decoded.codepoint < 0x80 ? size * 0.6f : size;
            offset += decoded.length;
        }
        return width;
    }
    };
    const auto status_of = [&measurer](const luil::text::line_break_policy policy) {
        const luil::label_config label { demo::line_break_label_config(std::u8string { demo::line_break_sample }, policy, 12.0f) };
        luil::text_area_element area { luil::ui_element_id { luil::application_element_kind(44) },
            luil::text_input_view { .text = std::u8string { demo::line_break_sample } }, demo::line_break_area_config(policy, std::make_shared<luil::text_area_wrap_cache>())
        };
        return demo::measure_line_break_status(policy, label, area, 1.0f, &measurer);
    };
    const std::u8string words { status_of(luil::text::line_break_policy::hangul_words) };
    const std::u8string syllables { status_of(luil::text::line_break_policy::unicode) };
    // 기본 정책은 첫 줄을 어절 끝에서 끊고, unicode 정책은 같은 폭을 음절 사이까지 채운다.
    REQUIRE(words == u8"정책 hangul_words · 라벨 5줄 · 칸 5줄 · 첫 줄 끝 '바꾸고'");
    REQUIRE(syllables == u8"정책 unicode · 라벨 5줄 · 칸 5줄 · 첫 줄 끝 '고 낱'");

    // 줄 끝 글자는 매단 공백을 뺀 끝 글자소 셋이다.
    REQUIRE(demo::line_break_line_end(u8"가나다라  ", 0, 14) == u8"나다라");
    REQUIRE(demo::line_break_line_end(u8"ab", 0, 2) == u8"ab");
    REQUIRE(demo::line_break_line_end(u8"ab", 2, 2).empty());
}
