#pragma once

#include "luil/text/string_table.h"
#include "text/library_text.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace luil_test {
    // 라이브러리 언어는 프로세스에 하나다. 바꾼 test가 끝에서 되돌려 다른 test에 새지 않게 한다.
    class text_languages_guard
    {
    public:
        text_languages_guard()
            : saved_ { luil::text_languages() }
        {
        }

        ~text_languages_guard()
        {
            if (saved_.has_value())
                luil::set_text_languages(*saved_);
            else
                luil::reset_text_languages();
        }

        text_languages_guard(const text_languages_guard&) = delete;
        text_languages_guard& operator=(const text_languages_guard&) = delete;

        // 언어 하나로 정한다.
        void use(std::u8string language) const
        {
            const std::vector<std::u8string> preferences { std::move(language) };
            luil::set_text_languages(preferences);
        }

    private:
        std::optional<std::vector<std::u8string>> saved_ {};
    };
}
