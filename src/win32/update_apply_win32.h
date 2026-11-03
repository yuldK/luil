#pragma once

#include "host/update_manifest.h"
#include "luil/app/updater.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

// 자동 업데이트 적용의 Win32 쪽이다 (docs/auto-update-apply-design.md).
//  - 앱 쪽: 받아 둔 새 exe를 도우미 표식 인자로 띄운다 (`launch_update_helper`). 앱은 그 뒤 스스로 끝난다.
//  - 도우미: 공개 `run_update_helper`가 옛 프로세스의 끝을 기다려 옛 exe를 같은 경로에서 바꾸고 다시 띄우며, 새 판이 곧 쓰러지면 되돌린다.
//  - 쓰기 시험: 실행 파일 폴더에 쓸 수 있는가 (결정 10).
// 공개하지 않는다. 앱은 `updater::apply`로 부르고, 시험 실행 파일(luil_update_probe)만 바로 부른다.
namespace luil::win32 {
    // 앱 쪽이 도우미를 띄울 때 넘기는 값이다.
    struct update_helper_launch
    {
        // 받아 둔 새 exe다. 이것이 도우미로 뜨고, 도우미는 자기 이미지를 옛 exe 자리에 놓는다.
        std::filesystem::path helper_executable {};
        // 바꿀 exe다. 앱은 이 프로세스의 실행 파일을 준다.
        std::filesystem::path old_executable {};
        std::filesystem::path download_directory {};
        update_digest sha256 {};
        std::u8string from_version {};
        std::u8string to_version {};
        update_apply_options options {};
    };

    // 자기 프로세스 손잡이를 `SYNCHRONIZE`·상속 가능으로 복제해 손잡이 목록 속성으로 그 하나만 물려 도우미를 띄운다 (결정 8).
    //  - 작업 폴더는 이 프로세스의 것이다. 띄웠으면 `AllowSetForegroundWindow(도우미)`를 걸고 참이다.
    //  - 시한이 규칙 밖이거나 인자에 NUL이 있거나 띄우지 못하면 거짓이고 `error`에 까닭을 적는다.
    //  - 받은 exe의 머리가 `runnable_executable_header`를 지나지 못하면 OS에 넘기지 않고 거짓이다.
    [[nodiscard]] bool launch_update_helper(const update_helper_launch& launch, std::u8string& error);

    // 파일 앞쪽 바이트가 이 빌드가 띄울 수 있는 Windows 실행 파일의 머리인가다 (MZ, PE 서명, 이 빌드와 같은 Machine, DLL이 아닌 실행 이미지).
    //  - 도우미로 띄우기 전에 거른다. PE가 아닌 파일을 `CreateProcessW`에 넘기면 OS가 사용자 화면에 "지원되지 않는 16비트 응용
    //    프로그램" 같은 상자를 띄운다. 해시가 맞아도 배포한 쪽이 틀린 파일을 올렸을 수 있다.
    //  - 아니면 거짓이고 `error`에 까닭을 적는다. `header`에는 PE 머리까지 들어 있어야 한다.
    [[nodiscard]] bool runnable_executable_header(std::span<const std::byte> header, std::u8string& error);

    // 폴더에 `CREATE_NEW | FILE_FLAG_DELETE_ON_CLOSE`로 시험 파일을 만들어 본다 (결정 10). 만들었으면 참이다. 시험 파일은 남지 않는다.
    [[nodiscard]] bool folder_writable(const std::filesystem::path& folder, std::u8string& error);
    // 이 프로세스 실행 파일의 폴더에 쓸 수 있는가다.
    [[nodiscard]] bool executable_folder_writable(std::u8string& error);

    // 도우미가 `update.lock`을 다시 잡아 보는 시한이다. 부모가 쥐던 것은 그 프로세스가 끝날 때 풀렸다.
    inline constexpr std::chrono::milliseconds update_lock_retry_limit { 5000 };
    // 옛 exe 바꾸기를 다시 해 보는 시한이다. 백신 검사가 잠깐 쥘 수 있다.
    inline constexpr std::chrono::milliseconds update_replace_retry_limit { 10000 };
    inline constexpr std::chrono::milliseconds update_retry_interval { 250 };
}
