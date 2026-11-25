#include "net/socket_no_sigpipe.h"

#ifdef __APPLE__
#include <sys/socket.h>
#endif

namespace luil::net {
    bool suppress_sigpipe(const int socket) noexcept
    {
        if (socket < 0)
            return false;
#ifdef __APPLE__
        const int on { 1 };
        return ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)) == 0;
#else
        return true;
#endif
    }
}
