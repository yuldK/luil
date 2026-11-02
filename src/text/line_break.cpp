#include "luil/text/line_break.h"

#include "luil/text/utf8_text.h"

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace luil::text {
    namespace {
        // UAX #14의 줄 나눔 분류다. LB1은 생성 때 풀어 AI·SG·XX·SA·CJ가 없다.
        enum class line_break_class : std::uint8_t
        {
            al,
            bk,
            cr,
            lf,
            nl,
            sp,
            zw,
            wj,
            gl,
            cm,
            zwj,
            ak,
            ap,
            as,
            b2,
            ba,
            bb,
            cb,
            cl,
            cp,
            eb,
            em,
            ex,
            h2,
            h3,
            hl,
            hy,
            id,
            in,
            is,
            jl,
            jt,
            jv,
            ns,
            nu,
            op,
            po,
            pr,
            qu,
            ri,
            sy,
            vf,
            vi
        };
        struct line_break_range { char32_t begin; char32_t end; line_break_class value; };
        struct codepoint_range { char32_t begin; char32_t end; };

#include "text/line_break_data.h"

        // LB28a가 분류가 아니라 글자로 지목하는 점선 원(◌)이다.
        constexpr char32_t dotted_circle { 0x25CC };

        template<typename range, std::size_t size>
        const range* find_range(const range (&ranges)[size], const char32_t value) noexcept
        {
            const auto found { std::lower_bound(std::begin(ranges), std::end(ranges), value,
                [](const range& item, const char32_t codepoint) { return item.end < codepoint; })
            };
            return found != std::end(ranges) && found->begin <= value ? found : nullptr;
        }

        template<std::size_t size>
        bool contains(const codepoint_range (&ranges)[size], const char32_t value) noexcept
        {
            // 표마다 첫 범위보다 앞이면 찾지 않는다. ASCII 글은 이 비교 하나로 끝난다.
            return value >= ranges[0].begin && find_range(ranges, value) != nullptr;
        }

        line_break_class class_of(const char32_t value) noexcept
        {
            if (value < 0x80)
                return ascii_line_break_classes[value];
            if (value >= 0xAC00 && value <= 0xD7A3)
                return (value - 0xAC00) % 28 == 0 ? line_break_class::h2 : line_break_class::h3;
            const auto* const found { find_range(line_break_ranges, value) };
            return found != nullptr ? found->value : line_break_class::al;
        }

        bool is_hangul(const line_break_class value) noexcept
        {
            using enum line_break_class;
            return value == h2 || value == h3 || value == jl || value == jv || value == jt;
        }

        // LB9가 결합 부호를 붙이지 않는 앞 글자다.
        bool keeps_marks_apart(const line_break_class value) noexcept
        {
            using enum line_break_class;
            return value == bk || value == cr || value == lf || value == nl || value == sp || value == zw;
        }

        bool is_east_asian(const char32_t value) noexcept
        {
            return contains(east_asian_ranges, value);
        }

        // LB9로 결합 부호를 붙인 글자 하나다. 분류와 글자 속성은 앞 글자(base)의 것이다.
        struct break_unit
        {
            std::size_t offset { 0 };
            char32_t codepoint { 0 };
            line_break_class value { line_break_class::al };
            // 마지막 코드포인트가 ZWJ다 (LB8a는 붙인 뒤에도 원래 글자를 본다).
            bool ends_with_zwj { false };
        };

        std::vector<break_unit> units_of(const std::u8string_view text, const line_break_policy policy)
        {
            std::vector<break_unit> units {};
            units.reserve(text.size());
            const bool ascii { std::all_of(text.begin(), text.end(), [](const char8_t value) { return value < 0x80; }) };
            for (std::size_t at { 0 }; at < text.size();)
            {
                utf8_decoded decoded { static_cast<char32_t>(text[at]), 1 };
                if (ascii == false)
                    decoded = utf8_decode(text, at);
                line_break_class value { class_of(decoded.codepoint) };
                if (policy == line_break_policy::hangul_words && is_hangul(value))
                    value = line_break_class::al;
                const bool mark { value == line_break_class::cm || value == line_break_class::zwj };
                // LB9: X (CM | ZWJ)*는 X다. 그 안에서는 바꾸지 않는다.
                if (mark && units.empty() == false && keeps_marks_apart(units.back().value) == false)
                    units.back().ends_with_zwj = value == line_break_class::zwj;
                else
                {
                    // LB10: 남은 CM·ZWJ는 AL이다.
                    units.push_back({ at, decoded.codepoint, mark ? line_break_class::al : value, value == line_break_class::zwj });
                }
                at += decoded.length;
            }
            return units;
        }

        enum class break_action
        {
            prohibited,
            allowed,
            mandatory
        };

        // `index` 앞에서 바꾸는지다. 앞쪽 문맥은 부르는 쪽이 걸으며 쥔다 (공백 행렬·숫자 행렬·RI 수).
        struct break_context
        {
            // 앞 글자부터 공백(SP)을 거슬러 건넌 자리의 글자다. 공백뿐이면 첫 글자다.
            std::size_t before_spaces { 0 };
            // 앞 글자가 NU (SY | IS)*로 끝난다 (LB25).
            bool numeric { false };
            // 앞 글자가 CL·CP이고 그 앞이 NU (SY | IS)*다 (LB25).
            bool numeric_close { false };
            // 앞에 이어진 RI의 수다 (LB30a).
            std::size_t regional { 0 };
        };

        break_action decide(const std::vector<break_unit>& units, const std::size_t index, const break_context& context) noexcept
        {
            using enum line_break_class;
            const break_unit& left { units[index - 1] };
            const break_unit& right { units[index] };
            const line_break_class a { left.value };
            const line_break_class b { right.value };
            const auto value_at = [&units](const std::size_t at) { return at < units.size() ? units[at].value : line_break_class::al; };
            const bool has_next { index + 1 < units.size() };
            const line_break_class next { value_at(index + 1) };
            const line_break_class anchor { units[context.before_spaces].value };
            const auto any = [](const line_break_class value, const auto... values) { return ((value == values) || ...); };

            // LB4·LB5: 줄 바꿈 글자 뒤에서 반드시 바꾼다. CR LF는 붙인다.
            if (a == bk)
                return break_action::mandatory;
            if (a == cr && b == lf)
                return break_action::prohibited;
            if (any(a, cr, lf, nl))
                return break_action::mandatory;
            // LB6·LB7: 줄 바꿈 글자와 공백 앞에서는 바꾸지 않는다.
            if (any(b, bk, cr, lf, nl, sp, zw))
                return break_action::prohibited;
            // LB8: ZW SP* ÷
            if (anchor == zw)
                return break_action::allowed;
            // LB8a: ZWJ ×
            if (left.ends_with_zwj)
                return break_action::prohibited;
            // LB11·LB12·LB12a: WJ와 GL은 붙인다. 공백·BA·HY 뒤의 GL만 떼어 낸다.
            if (a == wj || b == wj || a == gl)
                return break_action::prohibited;
            if (b == gl && any(a, sp, ba, hy) == false)
                return break_action::prohibited;
            // LB13: 닫는 부호·느낌표·슬래시 앞에서는 바꾸지 않는다.
            if (any(b, cl, cp, ex, sy))
                return break_action::prohibited;
            // LB14: OP SP* ×
            if (anchor == op)
                return break_action::prohibited;
            // LB15a: 여는 따옴표(Pi) 뒤는 공백이 있어도 붙인다. 따옴표 앞이 글의 처음이나 끊는 자리일 때다.
            if (anchor == qu && contains(initial_quote_ranges, units[context.before_spaces].codepoint)
                && (context.before_spaces == 0 || any(units[context.before_spaces - 1].value, bk, cr, lf, nl, op, qu, gl, sp, zw)))
                return break_action::prohibited;
            // LB15b: 닫는 따옴표(Pf) 앞은 그 뒤가 끝이나 끊는 자리일 때 붙인다.
            if (b == qu && contains(final_quote_ranges, right.codepoint)
                && (has_next == false || any(next, sp, gl, wj, cl, qu, cp, ex, is, sy, bk, cr, lf, nl, zw)))
                return break_action::prohibited;
            // LB15c·LB15d: 공백 뒤 IS NU는 떼고, 그 밖의 IS 앞은 붙인다.
            if (a == sp && b == is && next == nu && has_next)
                return break_action::allowed;
            if (b == is)
                return break_action::prohibited;
            // LB16·LB17: (CL | CP) SP* × NS, B2 SP* × B2
            if (any(anchor, cl, cp) && b == ns)
                return break_action::prohibited;
            if (anchor == b2 && b == b2)
                return break_action::prohibited;
            // LB18: 공백 뒤에서 바꾼다.
            if (a == sp)
                return break_action::allowed;
            // LB19: 여는 쪽이 아닌 따옴표 앞, 닫는 쪽이 아닌 따옴표 뒤는 붙인다.
            if (b == qu && contains(initial_quote_ranges, right.codepoint) == false)
                return break_action::prohibited;
            if (a == qu && contains(final_quote_ranges, left.codepoint) == false)
                return break_action::prohibited;
            // LB19a: 동아시아 글자로 둘러싸이지 않은 따옴표는 양쪽을 붙인다.
            if (b == qu)
            {
                if (is_east_asian(left.codepoint) == false)
                    return break_action::prohibited;
                if (has_next == false || is_east_asian(units[index + 1].codepoint) == false)
                    return break_action::prohibited;
            }
            if (a == qu)
            {
                if (is_east_asian(right.codepoint) == false)
                    return break_action::prohibited;
                if (index < 2 || is_east_asian(units[index - 2].codepoint) == false)
                    return break_action::prohibited;
            }
            // LB20: CB 앞뒤에서 바꾼다.
            if (a == cb || b == cb)
                return break_action::allowed;
            // LB20a: 낱말 처음의 하이픈 뒤는 붙인다.
            if ((a == hy || left.codepoint == 0x2010) && b == al
                && (index < 2 || any(units[index - 2].value, bk, cr, lf, nl, sp, zw, cb, gl)))
                return break_action::prohibited;
            // LB21: BA·HY·NS 앞과 BB 뒤는 붙인다.
            if (any(b, ba, hy, ns) || a == bb)
                return break_action::prohibited;
            // LB21a: HL (HY | 동아시아가 아닌 BA) × [^HL]
            if (index >= 2 && units[index - 2].value == hl && (a == hy || (a == ba && is_east_asian(left.codepoint) == false)) && b != hl)
                return break_action::prohibited;
            // LB21b·LB22: SY × HL, × IN
            if ((a == sy && b == hl) || b == in)
                return break_action::prohibited;
            // LB23·LB23a·LB24: 글자·숫자·접두·접미 기호 사이는 붙인다.
            if ((any(a, al, hl) && b == nu) || (a == nu && any(b, al, hl)))
                return break_action::prohibited;
            if ((a == pr && any(b, id, eb, em)) || (any(a, id, eb, em) && b == po))
                return break_action::prohibited;
            if ((any(a, pr, po) && any(b, al, hl)) || (any(a, al, hl) && any(b, pr, po)))
                return break_action::prohibited;
            // LB25: 숫자 표현(`$100`, `1,234.5`, `(12)%`) 안은 붙인다.
            if (any(b, po, pr) && (context.numeric || context.numeric_close))
                return break_action::prohibited;
            if (any(a, po, pr))
            {
                if (b == nu)
                    return break_action::prohibited;
                if (b == op && has_next && (next == nu || (next == is && index + 2 < units.size() && units[index + 2].value == nu)))
                    return break_action::prohibited;
            }
            if (any(a, hy, is) && b == nu)
                return break_action::prohibited;
            if (b == nu && context.numeric)
                return break_action::prohibited;
            // LB26·LB27: 한글 자모 음절은 붙인다 (`unicode` 정책에서만 남는다).
            if ((a == jl && any(b, jl, jv, h2, h3)) || (any(a, jv, h2) && any(b, jv, jt)) || (any(a, jt, h3) && b == jt))
                return break_action::prohibited;
            if ((any(a, jl, jv, jt, h2, h3) && b == po) || (a == pr && any(b, jl, jv, jt, h2, h3)))
                return break_action::prohibited;
            // LB28: 글자 사이는 붙인다.
            if (any(a, al, hl) && any(b, al, hl))
                return break_action::prohibited;
            // LB28a: 브라흐미계 음절(AK·AS·◌와 모음 기호)은 붙인다.
            const auto base = [](const break_unit& unit) { return unit.value == ak || unit.value == as || unit.codepoint == dotted_circle; };
            if (a == ap && (b == ak || b == as || right.codepoint == dotted_circle))
                return break_action::prohibited;
            if (base(left) && any(b, vf, vi))
                return break_action::prohibited;
            if (index >= 2 && base(units[index - 2]) && a == vi && (b == ak || right.codepoint == dotted_circle))
                return break_action::prohibited;
            if (base(left) && base(right) && next == vf && has_next)
                return break_action::prohibited;
            // LB29·LB30: 마침표 뒤 글자, 동아시아가 아닌 괄호 안팎은 붙인다.
            if (a == is && any(b, al, hl))
                return break_action::prohibited;
            if (any(a, al, hl, nu) && b == op && is_east_asian(right.codepoint) == false)
                return break_action::prohibited;
            if (a == cp && is_east_asian(left.codepoint) == false && any(b, al, hl, nu))
                return break_action::prohibited;
            // LB30a: 국기는 RI 두 개씩 붙인다.
            if (a == ri && b == ri && context.regional % 2 == 1)
                return break_action::prohibited;
            // LB30b: 피부색 수식은 붙인다.
            if (b == em && (a == eb || contains(unassigned_pictographic_ranges, left.codepoint)))
                return break_action::prohibited;
            // LB31
            return break_action::allowed;
        }
    }

    std::vector<line_break_opportunity> line_break_opportunities(const std::u8string_view text, const line_break_policy policy)
    {
        std::vector<line_break_opportunity> result {};
        if (text.empty())
            return result;
        const std::vector<break_unit> units { units_of(text, policy) };
        break_context context {};
        const auto advance = [&units, &context](const std::size_t index) {
            using enum line_break_class;
            const line_break_class value { units[index].value };
            if (value != sp)
                context.before_spaces = index;
            const bool numeric { context.numeric };
            context.numeric = value == nu || ((value == sy || value == is) && numeric);
            context.numeric_close = (value == cl || value == cp) && numeric;
            context.regional = value == ri ? context.regional + 1 : 0;
        };
        advance(0);
        for (std::size_t index { 1 }; index < units.size(); ++index)
        {
            const break_action action { decide(units, index, context) };
            if (action != break_action::prohibited)
                result.push_back({ units[index].offset, action == break_action::mandatory });
            advance(index);
        }
        result.push_back({ text.size(), true });
        return result;
    }

    bool line_break_hangs(const char32_t codepoint) noexcept
    {
        return contains(hanging_space_ranges, codepoint);
    }
}
