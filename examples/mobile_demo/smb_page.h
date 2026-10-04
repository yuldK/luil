#pragma once

// SMB 페이지다: 같은 네트워크의 NAS·Windows 공유에 붙어 폴더를 열거하고 파일을 앞부분부터 구간으로 읽는다. 답마다 걸린
// 시간과 연결을 새로 열었는지 다시 썼는지를 적어, 연결 다시 쓰기의 효과를 사람이 볼 수 있다.
// Android에서 LUIL_ENABLE_SMB를 켠 구성만 client가 있다. 그 밖의 구성에서는 없다는 사실만 적는다.
// 서버·계정은 사람이 적는다 — 이 페이지는 실제 서버에 붙어 보는 사람 손 확인의 자리다. "찾기"는 같은 네트워크의 SMB
// 서버를 보이고, 서버를 누르거나 서버만 적으면 공유 목록을 보이며, 공유를 누르면 그 공유를 연다.

#include "luil/net/smb_client.h"
#include "luil/text/text_edit.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/ui_interaction.h"
#include "mobile_demo/common.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace mobile_demo {
    // --- 이 페이지의 메시지 ---
    // 적은 주소의 폴더를 연다.
    struct smb_open_intent
    {};

    // 지금 폴더의 위 폴더로 간다.
    struct smb_up_intent
    {};

    // 열거한 항목 하나를 눌렀다. 폴더면 들어가고 파일이면 앞부분을 읽는다.
    struct smb_entry_intent
    {
        std::u8string name {};
        bool directory { false };
    };

    struct smb_encrypt_intent
    {
        bool encrypt { false };
    };

    // 암호 칸의 글을 보이거나 가린다.
    struct smb_reveal_intent
    {
        bool reveal { false };
    };

    // 읽던 파일의 다음 구간을 읽는다.
    struct smb_next_range_intent
    {};

    // 공유 목록에서 공유 하나를 눌렀다. 주소를 "서버/공유"로 바꾸고 연다.
    struct smb_share_intent
    {
        std::u8string name {};
    };

    // 같은 네트워크의 SMB 서버를 찾는다.
    struct smb_discover_intent
    {};

    // 찾은 서버 하나를 눌렀다. 주소를 그 서버로 바꾸고 공유 목록을 연다.
    struct smb_server_intent
    {
        std::u8string server {};
    };

    // 쉬는 연결을 닫는다 (단추, 그리고 앱이 뒤로 갈 때 delegate가 보낸다).
    struct smb_close_idle_intent
    {};

    // delegate가 앱 host가 선 뒤 세운 client다. logic thread가 이미 돌고 있어 직접 걸지 않고 메시지로 넘긴다.
    struct smb_client_intent
    {
        luil::net::smb_client* client { nullptr };
    };

    // client의 답이다 (delegate가 배달 thread에서 앱 메시지로 넘긴다).
    struct smb_result_intent
    {
        luil::net::smb_response response {};
    };

    // 페이지 상태와 비동기 전송을 잇는 다리다. 실제 client와 서버 없는 상태 시험이 같은 요청·답 경로를 쓴다.
    struct smb_page_transport
    {
        std::function<luil::net::smb_ticket(luil::net::smb_request, std::u8string&)> send {};
        std::function<void(luil::net::smb_ticket)> cancel {};
        std::function<void()> stop {};
        std::function<void()> close_idle {};
        std::function<luil::net::smb_ticket(luil::net::smb_discovery_request, std::u8string&)> discover {};
    };

    class smb_page
    {
    public:
        explicit smb_page(smb_page_transport transport = {});
        bool handle(const luil::app_message& message);
        [[nodiscard]] page_content build(float width, float scale);

        // client를 멈춘다. 답이 앱 host를 거쳐 오므로 host보다 먼저 부른다 (`stop_workers`).
        void stop()
        {
            if (transport_.stop != nullptr)
                transport_.stop();
        }

    private:
        void apply_edit(const luil::text_edit_request& request);
        void apply_composition(const luil::text_composition_event& event);
        // 적은 주소("서버/공유/폴더")를 연결 정보로 가른다. 공유는 빌 수 있다(서버만 적으면 공유 목록). 서버가 없으면 nullopt다.
        [[nodiscard]] std::optional<luil::net::smb_location> location() const;
        void cancel_pending();
        void send(luil::net::smb_operation operation, std::u8string path, std::uint64_t offset = 0, std::size_t length = 0);

        // 없으면(Windows, SMB를 끈 구성) 페이지가 그 사실만 적는다.
        smb_page_transport transport_ {};
        luil::text::text_edit_state address_ {};
        luil::text::text_edit_state user_ {};
        luil::text::text_edit_state password_ {};
        std::optional<luil::text_composition_event> composition_ {};
        bool encrypt_ { false };
        bool reveal_ { false };
        // 지금 보는 폴더다 (공유 안의 경로).
        std::u8string folder_ {};
        // 보낸 요청과 그 경로다. 늦게 온 답이 지금 화면을 덮지 않게 표로 맞춘다.
        luil::net::smb_ticket pending_ {};
        luil::net::smb_operation pending_operation_ { luil::net::smb_operation::list };
        std::u8string pending_path_ {};
        std::uint64_t pending_offset_ { 0 };
        std::size_t pending_length_ { 0 };
        // 읽고 있는 파일과 다음 구간의 시작이다. 마지막 구간이 요청보다 짧았으면(파일 끝) 더 읽지 않는다.
        std::u8string file_ {};
        std::uint64_t next_offset_ { 0 };
        bool more_ { false };
        std::vector<luil::net::smb_entry> entries_ {};
        // 보이는 공유(디스크이고 숨지 않은 것)와 걸러 낸 공유의 수다.
        std::vector<luil::net::smb_share> shares_ {};
        std::size_t hidden_shares_ { 0 };
        // 찾기로 찾은 서버다.
        std::vector<luil::net::smb_server_found> servers_ {};
        // 상태 한 줄과 그 아래 여러 줄로 나눠 적을 글(libsmb2의 오류 글, 파일 앞부분)이다. luil의 라벨은 한 줄이라
        // 긴 글은 페이지가 줄을 나눈다.
        std::u8string status_ { u8"주소를 적고 열기를 누른다." };
        std::u8string detail_ {};
    };
} // namespace mobile_demo
