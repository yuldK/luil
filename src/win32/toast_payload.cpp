#include "win32/toast_payload.h"

#include "host/notification_queue.h"

namespace luil::win32 {
    namespace {
        // XML 1.0의 `Char`가 받지 않는 글자인가. 바이트 하나로 끝나는 제어 문자와 U+FFFE·U+FFFF(3바이트)를 본다.
        [[nodiscard]] bool forbidden_control(const char8_t character) noexcept
        {
            return character < 0x20 && character != u8'\t' && character != u8'\n' && character != u8'\r';
        }

        [[nodiscard]] bool forbidden_noncharacter(const std::u8string_view text, const std::size_t offset) noexcept
        {
            return offset + 2 < text.size()
                && text[offset] == 0xEF
                && text[offset + 1] == 0xBF
                && (text[offset + 2] == 0xBE || text[offset + 2] == 0xBF);
        }

        void append_text_element(std::u8string& xml, const std::u8string_view text)
        {
            xml += u8"<text>";
            xml += escape_toast_text(text);
            xml += u8"</text>";
        }
    }

    std::u8string escape_toast_text(const std::u8string_view text)
    {
        std::u8string escaped {};
        escaped.reserve(text.size());
        for (std::size_t offset = 0; offset < text.size(); ++offset)
        {
            const char8_t character { text[offset] };
            if (forbidden_control(character))
                continue;
            if (forbidden_noncharacter(text, offset))
            {
                offset += 2;
                continue;
            }
            switch (character)
            {
            case u8'&':
                escaped += u8"&amp;";
                break;
            case u8'<':
                escaped += u8"&lt;";
                break;
            case u8'>':
                escaped += u8"&gt;";
                break;
            case u8'"':
                escaped += u8"&quot;";
                break;
            case u8'\'':
                escaped += u8"&apos;";
                break;
            default:
                escaped += character;
                break;
            }
        }
        return escaped;
    }

    std::u8string toast_image_url(const std::u8string_view path)
    {
        std::u8string url { u8"file:///" };
        for (const char8_t character : path)
        {
            switch (character)
            {
            case u8'\\':
                url += u8'/';
                break;
            case u8' ':
                url += u8"%20";
                break;
            case u8'#':
                url += u8"%23";
                break;
            case u8'%':
                url += u8"%25";
                break;
            default:
                url += character;
                break;
            }
        }
        return url;
    }

    std::u8string toast_launch_argument(const std::u8string_view id)
    {
        return std::u8string { toast_launch_prefix } + std::u8string { id };
    }

    std::optional<std::u8string> toast_id_from_launch_argument(const std::u8string_view argument)
    {
        if (argument.starts_with(toast_launch_prefix) == false)
            return std::nullopt;
        const std::u8string_view id { argument.substr(toast_launch_prefix.size()) };
        if (notification_id_valid(id) == false)
            return std::nullopt;
        return std::u8string { id };
    }

    std::u8string make_toast_xml(const toast_payload& payload)
    {
        // `launch`에는 id만 싣는다. 사용자 글이 활성화 인자로 다른 프로세스에 가지 않는다.
        std::u8string xml { u8"<toast launch=\"" };
        xml += escape_toast_text(toast_launch_argument(payload.id));
        xml += u8"\"><visual><binding template=\"ToastGeneric\">";
        append_text_element(xml, payload.title);
        if (payload.body.empty() == false)
            append_text_element(xml, payload.body);
        if (payload.image_path.empty() == false)
        {
            xml += u8"<image placement=\"appLogoOverride\" src=\"";
            xml += escape_toast_text(toast_image_url(payload.image_path));
            xml += u8"\"/>";
        }
        xml += u8"</binding></visual>";
        if (payload.silent)
            xml += u8"<audio silent=\"true\"/>";
        xml += u8"</toast>";
        return xml;
    }
}
