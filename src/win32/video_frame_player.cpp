#include "luil/win32/video_frame_player.h"

#include "text/library_text.h"

#include <windows.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <d3d11.h>
#include <d3d10.h>
#include <dxgiformat.h>
#include <mfidl.h>
#include <wmcodecdsp.h>
#include <wrl.h>
#include <shlwapi.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <cmath>
#include <cstdint>

namespace luil::win32 {
    namespace {
        using Microsoft::WRL::ComPtr;
        using library_text::key;

        class engine_events final : public Microsoft::WRL::RuntimeClass<
            Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFMediaEngineNotify>
        {
        public:
            HRESULT STDMETHODCALLTYPE EventNotify(DWORD event, DWORD_PTR parameter, DWORD) noexcept override
            {
                if (event == MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE)
                    SetEvent(reinterpret_cast<HANDLE>(parameter));
                return S_OK;
            }
        };
    }

    struct video_frame_player::engine
    {
        ComPtr<IMFMediaEngine> media {};
        ComPtr<ID3D11Device> device {};
        ComPtr<ID3D11DeviceContext> context {};
        ComPtr<IMFDXGIDeviceManager> manager {};
        ComPtr<ID3D11Texture2D> target {};
        ComPtr<ID3D11Texture2D> staging {};
        std::int64_t timestamp_100ns {};
        std::int64_t first_timestamp_100ns {};
        UINT width {};
        UINT height {};
        luil::ui_image frame {};
        luil::ui_image first_frame {};
        std::u8string error {};
        media_state state { media_state::empty };
        bool com_started {};
        bool mf_started {};

        ~engine()
        {
            if (media)
                (void)media->Shutdown();
            media.Reset();
            target.Reset();
            staging.Reset();
            manager.Reset();
            context.Reset();
            device.Reset();
            if (mf_started)
                (void)MFShutdown();
            if (com_started)
                CoUninitialize();
        }

        bool check(const HRESULT result, const key message)
        {
            if (SUCCEEDED(result))
                return true;
            char code[16] {};
            (void)std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
            const std::u8string hex { code, code + std::char_traits<char>::length(code) };
            error = library_text::format(library_strings(), message, std::array { string_argument { u8"code", hex } });
            state = media_state::failed;
            return false;
        }

        HRESULT create_device(const D3D_DRIVER_TYPE driver)
        {
            return D3D11CreateDevice(nullptr, driver, nullptr
                , D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT
                , nullptr, 0, D3D11_SDK_VERSION, device.ReleaseAndGetAddressOf()
                , nullptr, context.ReleaseAndGetAddressOf()
            );
        }

        void transfer()
        {
            LONGLONG timestamp {};
            const HRESULT tick { media->OnVideoStreamTick(&timestamp) };
            if (check(tick, key::video_error_timestamp) == false || tick != S_OK)
                return;
            DWORD native_width {};
            DWORD native_height {};
            if (check(media->GetNativeVideoSize(&native_width, &native_height), key::video_error_dimensions) == false)
                return;
            if (native_width == 0 || native_height == 0)
                return;
            // 불변 프레임의 복사량을 제한하면서 원본 종횡비를 유지한다.
            const double ratio { std::min(1.0, 1920.0 / std::max(native_width, native_height)) };
            const UINT w { std::max(1u, static_cast<UINT>(native_width * ratio)) };
            const UINT h { std::max(1u, static_cast<UINT>(native_height * ratio)) };
            if (w != width || h != height)
            {
                D3D11_TEXTURE2D_DESC desc {};
                desc.Width = w;
                desc.Height = h;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                if (check(device->CreateTexture2D(&desc, nullptr, target.ReleaseAndGetAddressOf()), key::video_error_surface) == false)
                    return;
                desc.BindFlags = 0;
                desc.Usage = D3D11_USAGE_STAGING;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                if (check(device->CreateTexture2D(&desc, nullptr, staging.ReleaseAndGetAddressOf()), key::video_error_staging) == false)
                    return;
                width = w;
                height = h;
            }
            const RECT destination { 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
            const MFARGB black { 0, 0, 0, 255 };
            if (check(media->TransferVideoFrame(target.Get(), nullptr, &destination, &black), key::video_error_transfer) == false)
                return;
            // 두 luil 렌더러가 모두 읽을 수 있도록 행 간격을 존중해 불변 이미지로 옮긴다.
            context->CopyResource(staging.Get(), target.Get());
            D3D11_MAPPED_SUBRESOURCE mapped {};
            if (check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), key::video_error_read_pixels) == false)
                return;
            const auto bytes { static_cast<std::size_t>(h - 1) * mapped.RowPitch + static_cast<std::size_t>(w) * 4 };
            frame = luil::make_bgrx_image(static_cast<int>(w), static_cast<int>(h), mapped.RowPitch
                , std::span { static_cast<const std::uint8_t*>(mapped.pData), bytes }
            );
            context->Unmap(staging.Get(), 0);
            if (check(frame.valid() ? S_OK : E_OUTOFMEMORY, key::video_error_copy) == false)
                return;
            timestamp_100ns = timestamp;
            if (first_frame.valid() == false)
            {
                first_frame = frame;
                first_timestamp_100ns = timestamp;
            }
        }
    };

    video_frame_player::video_frame_player() : engine_ { std::make_unique<engine>() }
    {
    }

    video_frame_player::~video_frame_player() = default;

    void video_frame_player::open(const std::filesystem::path& path)
    {
        engine_ = std::make_unique<engine>();
        auto& e { *engine_ };
        e.state = media_state::opening;
        std::error_code path_error {};
        const auto absolute { std::filesystem::absolute(path, path_error) };
        if (e.check(path.empty() || path_error ? E_INVALIDARG : S_OK, key::video_error_path) == false)
            return;
        if (e.check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), key::video_error_com) == false)
            return;
        e.com_started = true;
        if (e.check(MFStartup(MF_VERSION), key::video_error_mf) == false)
            return;
        e.mf_started = true;
        ComPtr<IMFMediaEngineClassFactory> factory {};
        ComPtr<IMFAttributes> attributes {};
        const auto events { Microsoft::WRL::Make<engine_events>() };
        UINT token {};
        HRESULT device { e.create_device(D3D_DRIVER_TYPE_HARDWARE) };
        if (FAILED(device))
            device = e.create_device(D3D_DRIVER_TYPE_WARP);
        if (e.check(device, key::video_error_d3d) == false
            || e.check(MFCreateDXGIDeviceManager(&token, &e.manager), key::video_error_device_manager) == false
            || e.check(e.manager->ResetDevice(e.device.Get(), token), key::video_error_device) == false)
            return;
        ComPtr<ID3D10Multithread> multi {};
        // Media Foundation도 같은 장치를 쓰므로 immediate context 접근을 직렬화한다.
        if (e.check(e.context.As(&multi), key::video_error_thread_protection) == false)
            return;
        (void)multi->SetMultithreadProtected(TRUE);
        if (e.check(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), key::video_error_factory) == false
            || e.check(MFCreateAttributes(&attributes, 3), key::video_error_attributes) == false
            || e.check(attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, e.manager.Get()), key::video_error_video_device) == false
            || e.check(attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, events.Get()), key::video_error_events) == false
            || e.check(attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM), key::video_error_pixel_format) == false
            || e.check(factory->CreateInstance(0, attributes.Get(), &e.media), key::video_error_frame_server) == false)
            return;
        // Windows 믹서에 다채널 AC-3의 디코딩된 PCM을 보낸다. 선택 효과로 넣으면 엔진이 우회할 수 있다.
        ComPtr<IMFMediaEngineEx> effects {};
        ComPtr<IMFTransform> resampler {};
        if (e.check(e.media.As(&effects), key::video_error_audio_effect) == false
            || e.check(CoCreateInstance(CLSID_CResamplerMediaObject, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&resampler)), key::video_error_resampler) == false
            || e.check(effects->InsertAudioEffect(resampler.Get(), FALSE), key::video_error_pcm_output) == false)
            return;

        DWORD length { 65536 };
        std::wstring url(length, L'\0');
        if (e.check(UrlCreateFromPathW(absolute.c_str(), url.data(), &length, 0), key::video_error_path) == false)
            return;
        BSTR source { SysAllocString(url.c_str()) };
        const HRESULT opened { source != nullptr ? e.media->SetSource(source) : E_OUTOFMEMORY };
        SysFreeString(source);
        if (e.check(opened, key::video_error_open))
            (void)e.check(e.media->Load(), key::video_error_read_file);
    }

    void video_frame_player::play()
    {
        auto& e { *engine_ };
        if (e.state != media_state::ready && e.state != media_state::stopped && e.state != media_state::ended)
            return;
        if (e.media->IsEnded() && e.check(e.media->SetCurrentTime(0), key::video_error_rewind) == false)
            return;
        if (e.check(e.media->Play(), key::video_error_play))
        {
            e.state = media_state::playing;
        }
    }

    void video_frame_player::stop()
    {
        auto& e { *engine_ };
        if (e.state != media_state::playing && e.state != media_state::ended && e.state != media_state::ready)
            return;
        if (e.check(e.media->Pause(), key::video_error_stop) && e.check(e.media->SetCurrentTime(0), key::video_error_rewind))
        {
            e.state = media_state::stopped;
            e.frame = e.first_frame;
            e.timestamp_100ns = e.first_timestamp_100ns;
        }
    }

    void video_frame_player::update()
    {
        auto& e { *engine_ };
        if (needs_tick() == false)
            return;
        ComPtr<IMFMediaError> error {};
        if (e.check(e.media->GetError(&error), key::video_error_state) == false)
            return;
        if (error)
        {
            const HRESULT code { error->GetExtendedErrorCode() };
            (void)e.check(FAILED(code) ? code : E_FAIL, key::video_error_read_media);
            return;
        }
        if (e.state == media_state::opening && e.media->GetReadyState() >= MF_MEDIA_ENGINE_READY_HAVE_CURRENT_DATA)
            e.state = media_state::ready;
        if (e.state == media_state::opening)
            return;
        e.transfer();
        if (e.state == media_state::playing && e.media->IsEnded())
        {
            e.state = media_state::ended;
        }
    }

    bool video_frame_player::needs_tick() const
    {
        return engine_->state == media_state::opening || engine_->state == media_state::playing;
    }

    std::chrono::milliseconds video_frame_player::position() const
    {
        if (engine_->state != media_state::playing && engine_->state != media_state::ended)
            return {};
        const double seconds { engine_->media->GetCurrentTime() };
        if (std::isfinite(seconds) == false || seconds <= 0 || seconds >= 9.0e15)
            return {};
        return std::chrono::milliseconds { static_cast<std::int64_t>(seconds * 1000.0) };
    }

    const luil::ui_image& video_frame_player::image() const
    {
        return engine_->frame;
    }

    media_state video_frame_player::state() const
    {
        return engine_->state;
    }

    std::int64_t video_frame_player::frame_timestamp_100ns() const
    {
        return engine_->timestamp_100ns;
    }

    video_frame_statistics video_frame_player::statistics() const
    {
        ComPtr<IMFMediaEngineEx> diagnostics {};
        if (engine_->media == nullptr || FAILED(engine_->media.As(&diagnostics)))
            return {};
        const auto count = [&](const MF_MEDIA_ENGINE_STATISTIC statistic) -> std::optional<std::uint32_t>
        {
            PROPVARIANT value {};
            std::optional<std::uint32_t> result {};
            if (SUCCEEDED(diagnostics->GetStatistics(statistic, &value)) && value.vt == VT_UI4)
                result = value.ulVal;
            PropVariantClear(&value);
            return result;
        };
        return { count(MF_MEDIA_ENGINE_STATISTIC_FRAMES_DROPPED), count(MF_MEDIA_ENGINE_STATISTIC_FRAMES_RENDERED) };
    }

    const std::u8string& video_frame_player::error() const
    {
        return engine_->error;
    }
}
