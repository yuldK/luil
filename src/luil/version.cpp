#include "luil/version.h"

namespace luil {
    const char* library_version() noexcept
    {
        return LUIL_VERSION_STRING;
    }
} // namespace luil
