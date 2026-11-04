#pragma once

#include "luil/app/hot_reload.h"

#include <optional>

// 핫 리로드의 OS를 모르는 몫이다: 파일 읽기, 한도, 해석, 오류 줄(공개 `load_hot_reload_file`)과 감시 묶음 가르기
// (docs/hot-reload-design.md의 "규칙"). 감시자를 쥐는 `hot_reloader`는 묶음을 여기에 넘겨 결과를 받기만 한다.
namespace luil {
    // 감시 묶음 하나를 그 파일의 결과로 옮긴다. 건넬 것이 없으면 nullopt다.
    //  - `end == lost`는 `lost`이고 `errors`가 묶음의 `error` 한 줄이다. `end == removed`는 건네지 않는다.
    //  - rescan, created, modified, 새 이름이 그 파일인 renamed는 파일을 다시 읽는다.
    //  - removed, 옛 이름이 그 파일인 renamed는 읽지 않고 `missing`이다.
    //  - 그 파일 이름의 항목이 없으면 건네지 않는다.
    //  - 던지지 않는다. 감시 thread의 `deliver`에서 던지면 감시자가 거절로 보고 같은 묶음을 끝없이 다시 낸다.
    [[nodiscard]] std::optional<hot_reload_update> read_hot_reload_batch(const file_change_batch& batch, const hot_reload_file& file, const hot_reload_limits& limits);
}
