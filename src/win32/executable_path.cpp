#include "win32/executable_path.h"

#include <windows.h>

namespace luil::win32 {
    std::wstring current_executable_path()
    {
        std::wstring executable(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD written { GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())) };
            if (written == 0)
                return {};
            // 버퍼를 다 채웠으면 잘린 것이다. 늘려 다시 읽는다.
            if (written < executable.size())
            {
                executable.resize(written);
                return executable;
            }
            executable.resize(executable.size() * 2);
        }
    }
}
