#include "win32/composition_device.h"

#include "win32/win32_error.h"

namespace luil::win32 {
    composition_host::~composition_host()
    {
        if (device_ != nullptr)
            device_->Release();
    }

    IDCompositionDevice* composition_host::acquire(std::u8string& error)
    {
        if (device_ != nullptr)
            return device_;
        if (attempted_)
        {
            error = u8"The DirectComposition device is unavailable.";
            return nullptr;
        }

        attempted_ = true;
        // D3D device를 넘기지 않는다 (헤더의 이유).
        const HRESULT result { DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&device_)) };
        if (FAILED(result))
        {
            device_ = nullptr;
            error = make_hresult_error(u8"Failed to create the DirectComposition device", result);
            return nullptr;
        }
        return device_;
    }
} // namespace luil::win32
