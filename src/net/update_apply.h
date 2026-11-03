#pragma once

#include "host/update_manifest.h"
#include "luil/app/updater.h"

#include <filesystem>
#include <string>
#include <vector>

// 자동 업데이트의 적용이 OS에 맡기는 자리다 (docs/auto-update-apply-design.md).
// `updater.cpp`는 두 플랫폼 묶음에 같이 들어 이 선언만 부른다 (`update_crypto.h`와 같은 꼴).
//  - Win32는 src/win32/update_apply_win32.cpp가 도우미를 띄우고 쓰기를 시험한다.
//  - Android는 src/android/update_apply_android.cpp가 스토어 주소를 연다.
namespace luil::net {
    // updater가 적용 한 번에 넘기는 값이다.
    struct update_handoff
    {
        // Win32: 받아 둔 `<sha256>.exe`다. 이것이 도우미로 뜬다.
        std::filesystem::path ready_file {};
        std::filesystem::path download_directory {};
        update_digest sha256 {};
        std::u8string from_version {};
        std::u8string to_version {};
        update_apply_options options {};
        // Android: 열 스토어 주소와 열어도 되는 scheme이다.
        std::u8string store_url {};
        std::vector<std::u8string> store_schemes {};
    };

    // 적용을 넘긴다. Win32는 도우미를 띄우고, Android는 스토어 주소를 연다. 넘겼으면 참이고 아니면 `error`에 까닭을 적는다.
    [[nodiscard]] bool hand_off_update(const update_handoff& handoff, std::u8string& error);

    // 바꿀 실행 파일의 폴더에 쓸 수 있는가다 (적용 문서 결정 10). Android는 바꾸지 않아 늘 참이다.
    [[nodiscard]] bool update_target_writable(std::u8string& error);

    // 바꿀 실행 파일, 곧 이 프로세스의 실행 파일이다. Android와 읽지 못한 때는 비었다.
    //  - `create`가 지난 적용이 옆에 남긴 `.old`·`.tmp`를 찾는 자리다.
    [[nodiscard]] std::filesystem::path update_target_path();
}
