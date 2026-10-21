#pragma once

#include "luil/app/file_dialog.h"

#include <windows.h>

namespace luil::win32 {
    [[nodiscard]] file_dialog_result run_file_dialog(HWND owner, const file_dialog_request& request);
} // namespace luil::win32
