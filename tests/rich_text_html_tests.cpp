#include "luil/ui/rich_text.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// 서식 글의 HTML 쓰기와 거르는 읽기다 (docs/rich-edit-exchange-design.md).
namespace {
    using luil::text::text_mark;

    [[nodiscard]] std::string narrow(const std::u8string_view text)
    {
        return { text.begin(), text.end() };
    }

    [[nodiscard]] luil::rich_text_fragment read(const std::u8string_view html)
    {
        return luil::rich_text_from_html(html);
    }

    // 서식 없는 글 조각이다.
    [[nodiscard]] luil::rich_text_fragment plain(const std::u8string_view text)
    {
        return { std::u8string { text }, {} };
    }

    [[nodiscard]] luil::rich_text_fragment marked(const std::u8string_view text, std::vector<luil::text::text_mark_run> runs, std::vector<luil::text::text_format_link> links = {})
    {
        return { std::u8string { text }, { std::move(runs), std::move(links) } };
    }

    void require_round_trip(const std::u8string_view text, const luil::text::text_format& format)
    {
        const std::u8string html { luil::rich_text_to_html(text, format) };
        INFO(narrow(html));
        const luil::rich_text_fragment back { luil::rich_text_from_html(html) };
        REQUIRE(narrow(back.text) == narrow(text));
        REQUIRE(back.format == format);
    }
}

TEST_CASE("Rich text written as HTML reads back as the same text and format", "[ui][rich-text][html]")
{
    // 서식 없는 글, 앞·뒤·연속 공백과 탭, 이스케이프할 글자, 줄 바꿈.
    require_round_trip(u8"plain", {});
    require_round_trip(u8"  two  spaces\tand tab  ", {});
    require_round_trip(u8"a & b < c > d \"q\" 'a'", {});
    require_round_trip(u8"\nfirst\n\nthird\n", {});
    require_round_trip(u8"crlf\r\nline", {});
    require_round_trip(u8"한글 글자 😀", {});

    // 굵게가 줄 바꿈을 걸치고, 서식이 겹치고, 링크 안에 서식이 있다.
    const std::u8string_view text { u8"bold\nline italic code link end" };
    luil::text::text_format format {};
    format.runs = {
        { 0, 9, text_mark::bold },
        { 10, 16, text_mark::italic | text_mark::underline },
        { 17, 21, text_mark::code },
        { 22, 26, text_mark::bold | text_mark::italic },
    };
    format.links = { { 22, 26, u8"https://example.com/?a=1&b=\"2\"" } };
    require_round_trip(text, format);

    // 맞닿은 두 링크는 주소가 다르면 둘이다.
    luil::text::text_format links {};
    links.links = { { 0, 2, u8"https://a.example/" }, { 2, 4, u8"mailto:b@example.com" } };
    require_round_trip(u8"abcd", links);
    REQUIRE(luil::rich_text_to_html(u8"", format).empty());
}

TEST_CASE("Rich text HTML escapes text and closes tags at every format edge", "[ui][rich-text][html]")
{
    luil::text::text_format format {};
    format.runs = { { 0, 3, text_mark::bold }, { 3, 6, text_mark::bold | text_mark::italic } };
    format.links = { { 3, 6, u8"https://x.example/?q=\"1\"&r=<2>" } };
    const std::u8string html { luil::rich_text_to_html(u8"a<b&c>\nd", format) };
    REQUIRE(narrow(html)
        == narrow(u8"<span style=\"white-space: pre-wrap;\"><b>a&lt;b</b><a href=\"https://x.example/?q=&quot;1&quot;&amp;r=&lt;2&gt;\"><b><i>&amp;c&gt;</i></b></a><br>d</span>"));
    // 코드는 굵게보다 바깥, 밑줄은 가장 안쪽이다.
    luil::text::text_format all {};
    all.runs = { { 0, 1, text_mark::bold | text_mark::italic | text_mark::underline | text_mark::code } };
    REQUIRE(narrow(luil::rich_text_to_html(u8"x", all)) == narrow(u8"<span style=\"white-space: pre-wrap;\"><code><b><i><u>x</u></i></b></code></span>"));
}

TEST_CASE("Reading HTML takes the allowed tags and style properties as marks", "[ui][rich-text][html]")
{
    REQUIRE(read(u8"<b>a</b><strong>b</strong>") == marked(u8"ab", { { 0, 2, text_mark::bold } }));
    REQUIRE(read(u8"<i>a</i><em>b</em><u>c</u><ins>d</ins>") == marked(u8"abcd", { { 0, 2, text_mark::italic }, { 2, 4, text_mark::underline } }));
    REQUIRE(read(u8"<code>a</code><kbd>b</kbd><samp>c</samp><tt>d</tt>") == marked(u8"abcd", { { 0, 4, text_mark::code } }));
    // 모르는 요소는 태그만 버린다.
    REQUIRE(read(u8"<font color=red><span>a</span></font><blink>b</blink>") == plain(u8"ab"));

    // `style`이 태그보다 앞선다. 감싸는 `<b>`를 `font-weight: normal`로 끄는 편집기가 있다.
    REQUIRE(read(u8"<b style=\"font-weight:normal\">a<span style=\"font-weight:700\">b</span></b>") == marked(u8"ab", { { 1, 2, text_mark::bold } }));
    REQUIRE(read(u8"<span style=\"font-weight: bold\">a</span><span style=\"font-weight:600\">b</span><span style=\"font-weight:500\">c</span>")
        == marked(u8"abc", { { 0, 2, text_mark::bold } }));
    REQUIRE(read(u8"<span style=\"FONT-STYLE: Italic !important; color: red\">a</span><span style=\"font-style:oblique 10deg\">b</span>")
        == marked(u8"ab", { { 0, 2, text_mark::italic } }));
    REQUIRE(read(u8"<span style=\"text-decoration: underline wavy\">a</span><u style=\"text-decoration-line:none\">b</u>")
        == marked(u8"ab", { { 0, 1, text_mark::underline } }));
    REQUIRE(read(u8"<i><span style=\"font-style:normal\">a</span>b</i>") == marked(u8"ab", { { 1, 2, text_mark::italic } }));
}

TEST_CASE("Reading HTML keeps only web and mail links", "[ui][rich-text][html]")
{
    REQUIRE(read(u8"<a href=\"https://example.com/\">a</a>") == marked(u8"a", {}, { { 0, 1, u8"https://example.com/" } }));
    REQUIRE(read(u8"<a href='  HTTP://Example.com/x  '>a</a>") == marked(u8"a", {}, { { 0, 1, u8"HTTP://Example.com/x" } }));
    REQUIRE(read(u8"<a href=mailto:a@example.com>a</a>") == marked(u8"a", {}, { { 0, 1, u8"mailto:a@example.com" } }));
    REQUIRE(read(u8"<a href=\"https://e.example/?a=1&amp;b=2\">a</a>") == marked(u8"a", {}, { { 0, 1, u8"https://e.example/?a=1&b=2" } }));
    // 다른 scheme과 상대 주소는 글만 남는다.
    REQUIRE(read(u8"<a href=\"javascript:alert(1)\">a</a>") == plain(u8"a"));
    REQUIRE(read(u8"<a href=\" JavaScript:alert(1)\">a</a>") == plain(u8"a"));
    REQUIRE(read(u8"<a href=\"data:text/html,x\">a</a>") == plain(u8"a"));
    REQUIRE(read(u8"<a href=\"file:///c:/x\">a</a>") == plain(u8"a"));
    REQUIRE(read(u8"<a href=\"/relative\">a</a><a>b</a>") == plain(u8"ab"));
    // 링크 안의 허용되지 않은 링크는 링크가 아니다.
    REQUIRE(read(u8"<a href=\"https://a.example/\">a<a href=\"javascript:x\">b</a></a>") == marked(u8"ab", {}, { { 0, 1, u8"https://a.example/" } }));
}

TEST_CASE("Reading HTML drops scripts, styles, heads, images and comments with their content", "[ui][rich-text][html]")
{
    REQUIRE(read(u8"a<script>document.write('<b>x</b>')</script>b") == plain(u8"ab"));
    REQUIRE(read(u8"a<STYLE type=text/css>b { color: red }</STYLE>b") == plain(u8"ab"));
    REQUIRE(read(u8"<html><head><title>T</title><meta charset=utf-8></head><body>a</body></html>") == plain(u8"a"));
    // 닫지 않은 머리는 본문이 닫는다.
    REQUIRE(read(u8"<head><style>x</style><body><b>a</b>") == marked(u8"a", { { 0, 1, text_mark::bold } }));
    REQUIRE(read(u8"a<template><b>t</b></template><svg><text>s</text><svg>n</svg></svg><math><mi>x</mi></math>b") == plain(u8"ab"));
    REQUIRE(read(u8"a<iframe src=x>i</iframe><noscript>n</noscript><object data=x><b>o</b></object>b") == plain(u8"ab"));
    REQUIRE(read(u8"a<img src=\"https://example.com/x.png\" alt=\"picture\">b") == plain(u8"ab"));
    REQUIRE(read(u8"<!doctype html><!-- note <b>x</b> -->a<![CDATA[c]]><?xml version=\"1.0\"?><!--[if gte mso 9]><b>x</b><![endif]-->b") == plain(u8"ab"));
    // 닫는 태그가 없는 스크립트는 끝까지 버린다.
    REQUIRE(read(u8"a<script>b") == plain(u8"a"));
    // 태그가 아닌 `<`는 글이다.
    REQUIRE(read(u8"1 < 2 <3") == plain(u8"1 < 2 <3"));
}

TEST_CASE("Reading HTML turns blocks into line breaks and table cells into tabs", "[ui][rich-text][html]")
{
    REQUIRE(read(u8"a<br>b<br/>c</br>d") == plain(u8"a\nb\nc\nd"));
    REQUIRE(read(u8"<p>one</p><p>two</p>three") == plain(u8"one\ntwo\nthree"));
    REQUIRE(read(u8"<div>a</div><div><br></div><div>b</div>") == plain(u8"a\n\nb"));
    REQUIRE(read(u8"<h1>T</h1><ul><li>x</li><li>y</li></ul><blockquote>q</blockquote>") == plain(u8"T\nx\ny\nq"));
    REQUIRE(read(u8"<table><tr><td>a</td><td>b</td></tr><tr><th>c</th><td>d</td></tr></table>e") == plain(u8"a\tb\nc\td\ne"));
    // 블록 처음과 끝의 줄 바꿈은 남지 않는다.
    REQUIRE(read(u8"<div><p>a</p></div>") == plain(u8"a"));
    // 줄 바꿈의 서식은 그 자리의 서식이다. 블록 경계는 서식이 없다.
    REQUIRE(read(u8"<b>a<br>b</b><p>c</p>") == marked(u8"a\nb\nc", { { 0, 3, text_mark::bold } }));
}

TEST_CASE("Reading HTML decodes character references and collapses white space", "[ui][rich-text][html]")
{
    REQUIRE(read(u8"&amp;&lt;&gt;&quot;&apos;&nbsp;&#65;&#x42;&#X43;&#68") == plain(u8"&<>\"'\u00A0ABCD"));
    // 모르는 이름과 `;` 없는 이름은 글 그대로다. 범위 밖 숫자는 대체 문자다.
    REQUIRE(read(u8"&copy; &amp &#0; &#xD800; &#x110000; &#99999999999;") == plain(u8"&copy; &amp \uFFFD \uFFFD \uFFFD \uFFFD"));
    // 접은 공백은 처음 만난 자리의 서식이다. 굵게 안의 앞 공백은 앞의 공백에 접히고 뒤 공백이 굵게로 남는다.
    REQUIRE(read(u8"  a \n\t b  <b> c </b> d  ") == marked(u8"a b c d", { { 4, 6, text_mark::bold } }));
    REQUIRE(read(u8"<p>  a  </p>  <p>  b  </p>") == plain(u8"a\nb"));
    // `pre`와 `white-space: pre-wrap`은 공백을 지킨다. CR LF는 줄 바꿈 하나다.
    REQUIRE(read(u8"<pre>  a\r\n\tb  </pre>") == plain(u8"  a\n\tb  "));
    REQUIRE(read(u8"<span style=\"white-space:pre-wrap\">x  y</span>  z") == plain(u8"x  y z"));
    REQUIRE(read(u8"<pre><span style=\"white-space: normal\">x  y</span></pre>") == plain(u8"x y"));
}

TEST_CASE("Reading HTML falls back on oversized or broken input and stops formatting below the depth limit", "[ui][rich-text][html]")
{
    luil::rich_text_html_limits small {};
    small.max_bytes = 8;
    REQUIRE(luil::rich_text_from_html(u8"<b>a</b>", small) == marked(u8"a", { { 0, 1, text_mark::bold } }));
    REQUIRE(luil::rich_text_from_html(u8"<b>ab</b>", small) == luil::rich_text_fragment {});
    const std::u8string broken { std::u8string { u8"<b>a" } + char8_t { 0xFF } + u8"</b>" };
    REQUIRE(read(broken) == luil::rich_text_fragment {});
    // 끝나지 않은 태그는 거기서 멈춘다.
    REQUIRE(read(u8"a<b class=\"x") == plain(u8"a"));

    luil::rich_text_html_limits shallow {};
    shallow.max_depth = 2;
    REQUIRE(luil::rich_text_from_html(u8"<b><i><u>a</u></i>b</b>c", shallow) == marked(u8"abc", { { 0, 1, text_mark::bold | text_mark::italic }, { 1, 2, text_mark::bold } }));
    // 한도 밖의 숨길 요소도 내용째 버린다.
    REQUIRE(luil::rich_text_from_html(u8"<i><i>a<svg>s</svg>b</i></i>", shallow) == marked(u8"ab", { { 0, 2, text_mark::italic } }));

    // 아주 깊은 글도 스택을 쌓지 않고 읽는다.
    std::u8string deep {};
    for (int index = 0; index < 100000; ++index)
        deep += u8"<span>";
    deep += u8"x";
    for (int index = 0; index < 100000; ++index)
        deep += u8"</span>";
    REQUIRE(read(deep) == plain(u8"x"));
}

TEST_CASE("HTML from a document editor keeps its bold, italic and link", "[ui][rich-text][html]")
{
    // 문서 편집기는 글 전체를 `font-weight: normal`인 `<b>`로 감싸고 서식을 `style`로만 싣는다.
    const std::u8string_view html {
        u8"<meta charset=\"utf-8\"><b style=\"font-weight:normal;\" id=\"docs-internal-guid-1\"><p dir=\"ltr\" style=\"line-height:1.38;margin-top:0pt;\">"
        u8"<span style=\"font-size:11pt;font-weight:700;\">Bold</span><span style=\"font-size:11pt;font-weight:400;\"> and </span>"
        u8"<span style=\"font-size:11pt;font-style:italic;\">italic</span></p><p dir=\"ltr\"><a href=\"https://example.com/\" style=\"text-decoration:none;\">"
        u8"<span style=\"text-decoration:underline;\">link</span></a></p></b>"
    };
    const luil::rich_text_fragment fragment { read(html) };
    REQUIRE(narrow(fragment.text) == "Bold and italic\nlink");
    REQUIRE(fragment.format.runs == std::vector<luil::text::text_mark_run> { { 0, 4, text_mark::bold }, { 9, 15, text_mark::italic }, { 16, 20, text_mark::underline } });
    REQUIRE(fragment.format.links == std::vector<luil::text::text_format_link> { { 16, 20, u8"https://example.com/" } });
}
