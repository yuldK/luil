#include "ui/font_synthesis.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkFont.h"
#include "include/core/SkFontArguments.h"
#include "include/core/SkFontParameters.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkSpan.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        constexpr SkFourByteTag weight_tag { SkFontArguments::VariationPosition::Coordinate::wght };
    }

    bool weight_axis_range(const SkTypeface& face, float& minimum, float& maximum)
    {
        const int count { face.getVariationDesignParameters({}) };
        if (count <= 0)
            return false;
        std::vector<SkFontParameters::Variation::Axis> axes(static_cast<std::size_t>(count));
        if (face.getVariationDesignParameters(axes) != count)
            return false;
        for (const auto& axis : axes)
        {
            if (axis.tag != weight_tag || axis.max <= axis.min)
                continue;
            minimum = axis.min;
            maximum = axis.max;
            return true;
        }
        return false;
    }

    int achieved_weight(const SkTypeface& face)
    {
        const int count { face.getVariationDesignPosition({}) };
        if (count > 0)
        {
            std::vector<SkFontArguments::VariationPosition::Coordinate> position(static_cast<std::size_t>(count));
            if (face.getVariationDesignPosition(position) == count)
            {
                for (const auto& coordinate : position)
                {
                    if (coordinate.axis == weight_tag && std::isfinite(coordinate.value))
                        return std::clamp(static_cast<int>(std::lround(coordinate.value)), 1, 1000);
                }
            }
        }
        return std::clamp(face.fontStyle().weight(), 1, 1000);
    }

    font_synthesis synthesis_for(const SkTypeface& face, const font_style requested)
    {
        // 관리자가 이미 흉내 낸 face는 다시 메우지 않는다 — DirectWrite는 Bold가 없는 가족에 Bold를 물으면 스스로
        // 덧칠하는 face(simulation)를 돌려준다. 거기에 또 덧칠하면 두 번 굵어진다.
        const int threshold { font_weight_value(synthetic_bold_threshold) };
        return {
            .embolden = font_weight_value(requested.weight) >= threshold
                && achieved_weight(face) < threshold
                && face.isSyntheticBold() == false,
            .skew = requested.slant == font_slant::italic
                && face.fontStyle().slant() == SkFontStyle::kUpright_Slant
                && face.isSyntheticOblique() == false,
        };
    }

    void use_face(SkFont& font, sk_sp<SkTypeface> face, const font_style requested)
    {
        if (face == nullptr)
            return;
        const font_synthesis synthesis { synthesis_for(*face, requested) };
        font.setTypeface(std::move(face));
        font.setEmbolden(synthesis.embolden);
        font.setSkewX(synthesis.skew ? synthetic_skew : 0.0f);
    }

    font_style font_style_of(const SkFont& font)
    {
        const SkTypeface* const face { font.getTypeface() };
        int weight { face != nullptr ? achieved_weight(*face) : font_weight_value(font_weight::regular) };
        // 덧칠은 Bold 하나만 흉내 낸다. 덧칠한 글은 Bold를 요청한 글로 읽는다.
        if (font.isEmbolden())
            weight = std::max(weight, font_weight_value(font_weight::bold));
        if (face != nullptr && face->isSyntheticBold())
            weight = std::max(weight, font_weight_value(font_weight::bold));
        const bool leaning {
            (face != nullptr && (face->fontStyle().slant() != SkFontStyle::kUpright_Slant || face->isSyntheticOblique()))
            || font.getSkewX() != 0.0f
        };
        return { static_cast<font_weight>(weight), leaning ? font_slant::italic : font_slant::upright };
    }
}
