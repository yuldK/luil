#pragma once

#include "host/hotkey_sync.h"
#include "luil/app/global_hotkey.h"
#include "luil/win32/win32_window.h"

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>

// Carbon의 불투명 핸들이다. 헤더를 plain C++에서도 들이도록 `Carbon.h` 없이 이름만 둔다.
struct OpaqueEventHotKeyRef;
struct OpaqueEventHandlerRef;

namespace luil::macos {
    // 전역 단축키다 (docs/global-hotkey-design.md의 macOS 갈래). `RegisterEventHotKey`로 걸고 앱 event target의
    // `kEventHotKeyPressed`로 받는다. 손쉬운 사용(Accessibility) 권한이 들지 않는다.
    //  - 메인 thread에서만 만들고 부르고 없앤다. 눌림도 메인 thread의 event 처리 안에서 온다.
    //  - 어떤 호출을 할지는 core의 `hotkey_sync`가 고른다. 여기는 Carbon을 부르고 결과를 되돌리고 보고를 넘긴다.
    //  - 조합은 Windows 식이다. Ctrl이 Command, Alt가 Option, Shift가 Shift다 (`carbon_hotkey_modifiers_of`).
    //    Command가 macOS의 주 단축키 수정자라 앱 안 단축키(`primary_shortcut`)와 같은 짝이다.
    //  - 걸러내는 조합은 `classify_hotkey_chord`의 것 그대로다. Carbon 가상 키가 없는 키(`carbon_key_of`)는 `invalid_key`다.
    //    macOS가 따로 쥔 조합은 미리 거르지 않는다. Command+Tab·Command+Space는 Carbon이 `eventHotKeyExistsErr`로 답해
    //    `conflict`다 (실측). Command+Shift+3 같은 화면 찍기 조합은 걸린다.
    //  - 충돌은 같은 프로세스의 등록과 `kEventHotKeyExclusive`로 건 다른 프로세스만 보인다 (`eventHotKeyExistsErr`).
    //    배타 없이 건 앱과 겹쳐도 `registered`다.
    class hotkey_host
    {
    public:
        hotkey_host();
        hotkey_host(const hotkey_host&) = delete;
        hotkey_host& operator=(const hotkey_host&) = delete;
        // 건 것을 모두 풀고 처리기를 거둔다.
        ~hotkey_host();

        // 눌림이다. 걸린 항목의 id를 준다. host가 그 순간의 frame에서 같은 id의 `pressed`를 찾아 부른다.
        //  - Carbon 처리기 안에서 곧바로 부른다. 던지면 Carbon을 건너지 않게 여기서 삼킨다 — host가 먼저 잡아 알린다.
        void set_pressed(std::function<void(const std::u8string& id)> pressed);

        // 결과 보고다. 항목마다 결과가 바뀔 때 한 번이다 (`hotkey_sync`가 고른다). `update` 안에서 곧바로 부른다.
        void set_report(std::function<void(const win32::hotkey_report& report)> report);

        // 바라는 목록을 OS에 맞춘다. frame마다 불러도 된다 (같으면 아무것도 하지 않는다).
        //  - `key_capture`가 참인 동안 건 것을 모두 풀고(알리지 않는다) 거짓이 되면 다시 건다.
        void update(std::span<const global_hotkey> wanted, bool key_capture = false);

        // 앱이 활성화됐다. 다음 `update`가 `conflict`·`failed` 항목을 한 번 다시 건다.
        void reactivated() noexcept
        {
            sync_.reactivated();
        }

        // 건 것을 모두 푼다. 다음 `update`가 처음부터 다시 건다.
        void release_all();

        // Carbon 처리기의 입구다. 이 객체의 눌림이면 참이다.
        [[nodiscard]] bool handle_pressed(std::uint32_t signature, std::uint32_t number);

    private:
        void execute(const hotkey_call& call);

        hotkey_sync sync_ {};
        // OS 번호와 그 등록이다. 걸린 것만 있다.
        std::map<int, OpaqueEventHotKeyRef*> registered_ {};
        OpaqueEventHandlerRef* handler_ { nullptr };
        // `EventHotKeyID::signature`다. 객체마다 달라 번호가 같아도 다른 객체의 눌림을 가른다.
        std::uint32_t signature_ { 0 };
        std::function<void(const std::u8string&)> pressed_ {};
        std::function<void(const win32::hotkey_report&)> report_ {};
    };
}
