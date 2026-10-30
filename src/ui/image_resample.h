#pragma once

#include "include/core/SkBitmap.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSize.h"

#include <functional>

namespace luil {
    // 고품질 축소의 결과다.
    enum class image_resample_result
    {
        done,
        cancelled,
        out_of_memory,
    };

    // premul 8비트 네 채널(N32·RGBA·BGRA, 알파는 넷째 바이트) 그림을 `target`으로 줄인다 (image-frames-scaling-design.md).
    //  - 축마다 따로, 그 축이 목표의 두 배 이상인 동안 둘씩 평균해 반으로 줄이고, 남은 두 배 미만은 Mitchell–Netravali
    //    (B = C = 1/3) 3차 필터로 맞춘다. 늘리지 않는다 — 목표가 원본보다 큰 축은 원본 그대로다.
    //  - 결과는 같은 색 형식의 premul이다. 필터의 음의 꼬리로 넘친 값은 0–255로, 색은 알파 이하로 자른다.
    //  - `cancelled`는 각 단계의 출력 `image_downscale_cancel_rows`줄마다 묻는다 (첫 줄 포함). 비어 있으면 묻지 않는다.
    [[nodiscard]] image_resample_result downscale_premultiplied(const SkPixmap& source, SkISize target, const std::function<bool()>& cancelled, SkBitmap& output);
}
