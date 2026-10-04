#include "luil/net/smb_client.h"

#include <utility>

// SMB를 세우지 않은 구성(Windows, `LUIL_ENABLE_SMB`를 끈 Android)의 client다. 만들지 않고 이유를 답한다.
namespace luil::net {
    namespace {
        constexpr std::u8string_view unsupported_message { u8"SMB is built only on Android with LUIL_ENABLE_SMB=ON." };
    } // namespace

    struct smb_client::engine
    {};

    std::unique_ptr<smb_client> smb_client::create(smb_client_config config, std::u8string& error)
    {
        static_cast<void>(config);
        error = unsupported_message;
        return nullptr;
    }

    bool smb_client::supported() noexcept
    {
        return false;
    }

    smb_client::smb_client(std::unique_ptr<engine> implementation) noexcept
        : engine_ { std::move(implementation) }
    {}

    smb_client::~smb_client() = default;

    smb_ticket smb_client::send(smb_request request, std::u8string& error)
    {
        static_cast<void>(request);
        error = unsupported_message;
        return {};
    }

    void smb_client::cancel(const smb_ticket ticket) noexcept
    {
        static_cast<void>(ticket);
    }

    void smb_client::cancel_all() noexcept
    {}

    void smb_client::stop() noexcept
    {}

    std::size_t smb_client::in_flight() const noexcept
    {
        return 0;
    }

    void smb_client::close_idle() noexcept
    {}
} // namespace luil::net
