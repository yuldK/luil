// smb2.h는 표준 정수·시각 형을 스스로 들이지 않는다. 앞에 둔다.
#include <cstdint>
#include <ctime>

// libsmb2.h는 smb2.h의 정의(GUID 크기, lease 키)를 먼저 요구한다. 정렬이 순서를 바꾸지 않게 묶음을 나눈다.
#include <smb2/smb2.h>

#include <smb2/libsmb2.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

// 기기 SMB 시험의 서버다 (tests/smb_client_tests.cpp가 띄운다).
//
// libsmb2의 서버 쪽 위에 메모리 안의 공유 하나를 둔다 (`IPC$` 밖의 공유 이름은 모두 이 공유다). 시험 프로세스와 따로
// 돈다 — libsmb2의 서버 고리는 프로세스의 모든 libsmb2 문맥을 돌아, 한 프로세스에 두면 client 문맥까지 건드린다.
//  - 사용자는 `reader`, 암호는 `secret`이다. 틀린 암호는 STATUS_LOGON_FAILURE로 답한다. 손으로 볼 때를 위해 guest도
//    받는다 (mobile demo를 이 서버에 붙여 볼 때 사용자 칸을 비운다).
//  - `drop-once` 폴더를 처음 열면 답하지 않고 연결을 끊는다 (끊긴 연결의 재시도 자리). 그다음부터는 빈 폴더다.
//  - `slow` 폴더는 열기에 2초가 걸린다 (취소·정지 자리). 그동안 서버 전체가 멈춘다.
//  - 인자는 들을 포트 하나다. 시험이 SIGKILL로 거둔다.
namespace {
    struct node
    {
        bool directory { false };
        std::vector<std::uint8_t> data {};
        std::vector<std::string> children {};
    };

    struct handle
    {
        std::string path {};
        // 다음에 낼 폴더 항목의 차례다.
        std::size_t next { 0 };
    };

    std::map<std::string, node> nodes {};
    std::map<std::uint64_t, handle> handles {};
    std::uint64_t next_handle { 1 };
    bool dropped { false };

    void build_share()
    {
        std::vector<std::uint8_t> big(1024u * 1024u);
        for (std::size_t index { 0 }; index < big.size(); ++index)
            big[index] = static_cast<std::uint8_t>(index & 0xffu);
        const std::string note { "hello from smb\n" };
        nodes[""] = node { true, {}, { "albums", "big.bin", "drop-once", "note.txt", "slow" } };
        nodes["albums"] = node { true, {}, { "a.jpg" } };
        nodes["albums/a.jpg"] = node { false, std::vector<std::uint8_t>(100, 0x5a), {} };
        nodes["big.bin"] = node { false, std::move(big), {} };
        nodes["drop-once"] = node { true, {}, {} };
        nodes["note.txt"] = node { false, std::vector<std::uint8_t>(note.begin(), note.end()), {} };
        nodes["slow"] = node { true, {}, {} };
    }

    // 요청의 이름(`\`로 나뉜 UTF-8)을 `/`로 나눈 경로로 바꾼다.
    [[nodiscard]] std::string path_of(const char* const name)
    {
        std::string path { name != nullptr ? name : "" };
        for (char& character : path)
            if (character == '\\')
                character = '/';
        while (path.empty() == false && path.front() == '/')
            path.erase(path.begin());
        return path;
    }

    [[nodiscard]] std::uint64_t id_of(const smb2_file_id& file_id)
    {
        std::uint64_t value { 0 };
        std::memcpy(&value, file_id, sizeof(value));
        return value;
    }

    int on_authorize(smb2_server*, smb2_context* const context, const char* const user, const char*, const char*)
    {
        if (user == nullptr || user[0] == '\0' || std::strcmp(user, "guest") == 0)
            return 0;
        if (std::strcmp(user, "reader") != 0)
            return -1;
        smb2_set_user(context, user);
        smb2_set_password(context, "secret");
        return 0;
    }

    int on_session(smb2_server*, smb2_context*)
    {
        return 0;
    }

    int on_logoff(smb2_server*, smb2_context*)
    {
        return 0;
    }

    // libsmb2의 서버는 끊긴 연결을 거둘 때 기다리던 요청의 처리기를 빈 요청으로 부른다. 처리기마다 그것을 거른다.
    int on_tree_connect(smb2_server*, smb2_context*, smb2_tree_connect_request* const request, smb2_tree_connect_reply* const reply)
    {
        if (request == nullptr || reply == nullptr)
            return -1;
        // 경로는 UTF-16 `\\서버\공유`다. 마지막 조각만 ASCII로 본다.
        std::string share {};
        for (std::uint16_t index { 0 }; index < request->path_length / 2u; ++index)
        {
            const std::uint16_t unit { request->path[index] };
            if (unit == u'\\')
                share.clear();
            else
                share.push_back(static_cast<char>(unit < 0x80u ? unit : u'?'));
        }
        for (char& character : share)
            if (character >= 'a' && character <= 'z')
                character = static_cast<char>(character - 'a' + 'A');
        if (share == "IPC$")
        {
            reply->share_type = SMB2_SHARE_TYPE_PIPE;
            reply->maximal_access = 0x1f00a9;
            return 0;
        }
        reply->share_type = SMB2_SHARE_TYPE_DISK;
        reply->maximal_access = 0x101f01ff;
        return 0;
    }

    int on_tree_disconnect(smb2_server*, smb2_context*, const uint32_t)
    {
        return 0;
    }

    int on_create(smb2_server*, smb2_context* const context, smb2_create_request* const request, smb2_create_reply* const reply)
    {
        if (request == nullptr || reply == nullptr)
            return -1;
        const std::string path { path_of(request->name) };
        if (path == "drop-once" && dropped == false)
        {
            // 답하지 않고 끊는다. client는 쓰던 연결이 끊긴 것을 본다.
            dropped = true;
            smb2_close_context(context);
            return 1;
        }
        if (path == "slow")
            std::this_thread::sleep_for(std::chrono::seconds { 2 });
        const auto found { nodes.find(path) };
        if (found == nodes.end())
            return -1;
        const std::uint64_t id { next_handle++ };
        handles[id] = handle { path, 0 };
        reply->create_action = 1;
        reply->file_attributes = found->second.directory ? SMB2_FILE_ATTRIBUTE_DIRECTORY : SMB2_FILE_ATTRIBUTE_NORMAL;
        reply->end_of_file = found->second.data.size();
        reply->allocation_size = found->second.data.size();
        std::memcpy(reply->file_id, &id, sizeof(id));
        return 0;
    }

    int on_close(smb2_server*, smb2_context*, smb2_close_request* const request, smb2_close_reply* const reply)
    {
        if (request == nullptr || reply == nullptr)
            return -1;
        handles.erase(id_of(request->file_id));
        std::memset(reply, 0, sizeof(*reply));
        return 0;
    }

    int on_read(smb2_server*, smb2_context*, smb2_read_request* const request, smb2_read_reply* const reply)
    {
        if (request == nullptr || reply == nullptr)
            return -1;
        const auto opened { handles.find(id_of(request->file_id)) };
        if (opened == handles.end())
            return -1;
        const std::vector<std::uint8_t>& data { nodes[opened->second.path].data };
        const std::uint64_t begin { std::min<std::uint64_t>(request->offset, data.size()) };
        const std::uint64_t length { std::min<std::uint64_t>(request->length, data.size() - begin) };
        reply->data_length = static_cast<uint32_t>(length);
        reply->data_remaining = 0;
        reply->data = nullptr;
        if (length != 0)
        {
            reply->data = static_cast<uint8_t*>(std::malloc(length));
            if (reply->data == nullptr)
                return -1;
            std::memcpy(reply->data, data.data() + begin, length);
        }
        return 0;
    }

    int on_ioctl(smb2_server*, smb2_context*, smb2_ioctl_request* const request, smb2_ioctl_reply* const reply)
    {
        if (request == nullptr || reply == nullptr)
            return -1;
        std::memset(reply, 0, sizeof(*reply));
        reply->ctl_code = request->ctl_code;
        std::memcpy(reply->file_id, request->file_id, SMB2_FD_SIZE);
        return request->ctl_code == SMB2_FSCTL_VALIDATE_NEGOTIATE_INFO ? 0 : 1;
    }

    int on_nothing(smb2_server*, smb2_context*)
    {
        return 0;
    }

    int on_query_directory(smb2_server*, smb2_context*, smb2_query_directory_request* const request, smb2_query_directory_reply* const reply)
    {
        if (request == nullptr || reply == nullptr)
            return -1;
        // libsmb2의 서버는 항목을 이 구조체(64비트로 맞춘 칸)로 받아 선 위의 꼴로 바꾼다. 빈 답은
        // STATUS_NO_MORE_FILES다. 한 답에 여럿을 실으면 libsmb2가 다음 항목 자리를 처음부터 센 값으로 적어 client가
        // 읽지 못한다 — 그래서 답마다 하나씩 내고, client가 다 받을 때까지 다시 묻는다.
        reply->output_buffer = nullptr;
        reply->output_buffer_length = 0;
        const auto opened { handles.find(id_of(request->file_id)) };
        if (opened == handles.end())
            return 0;
        const node& folder { nodes[opened->second.path] };
        if (opened->second.next >= folder.children.size())
            return 0;
        const std::size_t index { opened->second.next++ };
        const std::size_t slot { (sizeof(smb2_fileidbothdirectoryinformation) + 7u) & ~std::size_t { 7u } };
        auto* const buffer { static_cast<uint8_t*>(std::calloc(1, slot)) };
        if (buffer == nullptr)
            return -1;
        const std::string& name { folder.children[index] };
        const std::string child { opened->second.path.empty() ? name : opened->second.path + "/" + name };
        const node& item { nodes[child] };
        auto* const entry { reinterpret_cast<smb2_fileidbothdirectoryinformation*>(buffer) };
        entry->file_index = static_cast<uint32_t>(index);
        entry->end_of_file = item.data.size();
        entry->allocation_size = item.data.size();
        entry->file_attributes = item.directory ? SMB2_FILE_ATTRIBUTE_DIRECTORY : SMB2_FILE_ATTRIBUTE_NORMAL;
        // 이름은 서버가 답을 지을 때까지 살아 있어야 한다. 표의 이름 칸을 그대로 가리킨다.
        entry->name = name.c_str();
        reply->output_buffer = buffer;
        reply->output_buffer_length = static_cast<uint32_t>(slot);
        return 0;
    }

    smb2_server_request_handlers handlers {};
    smb2_server server {};

    void on_new_client(smb2_context* const context, void*)
    {
        smb2_set_version(context, SMB2_VERSION_ANY);
    }
}

int main(const int argc, char** const argv)
{
    if (argc < 2)
        return 2;
    build_share();
    handlers.authorize_user = on_authorize;
    handlers.session_established = on_session;
    handlers.logoff_cmd = on_logoff;
    handlers.tree_connect_cmd = on_tree_connect;
    handlers.tree_disconnect_cmd = on_tree_disconnect;
    handlers.create_cmd = on_create;
    handlers.close_cmd = on_close;
    handlers.read_cmd = on_read;
    handlers.ioctl_cmd = on_ioctl;
    handlers.cancel_cmd = on_nothing;
    handlers.echo_cmd = on_nothing;
    handlers.query_directory_cmd = on_query_directory;
    server.port = static_cast<uint16_t>(std::atoi(argv[1]));
    server.handlers = &handlers;
    server.allow_anonymous = 1;
    std::strcpy(server.hostname, "luil-test");
    return smb2_serve_port(&server, 16, on_new_client, nullptr) == 0 ? 0 : 1;
}
