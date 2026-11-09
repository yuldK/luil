#include "luil/win32/media_player.h"

#include "win32/utf8.h"
#include <windows.h>
#include <mfplay.h>
#include <mfapi.h>
#include <mferror.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <limits>
#include <mutex>
#include <utility>

namespace luil::win32 {
    namespace {
        using Microsoft::WRL::ComPtr;

        bool result(const HRESULT value, std::u8string& error)
        {
            if (SUCCEEDED(value))
                return true;
            error = u8"Media Foundation operation failed.";
            return false;
        }

        struct media_callbacks
        {
            std::recursive_mutex gate {};
            std::function<void(media_event)> deliver {};
            std::atomic<ULONG_PTR> generation { 0 };
            media_state state { media_state::empty };
            bool stopped { false };
        };

        media_event read_snapshot(IMFPMediaPlayer* const player, const media_state state)
        {
            media_event event { .state = state };
            PROPVARIANT value {};
            if (SUCCEEDED(player->GetPosition(MFP_POSITIONTYPE_100NS, &value)))
            {
                if (value.vt == VT_I8)
                    event.position = std::chrono::milliseconds { value.hVal.QuadPart / 10000 };
                (void)PropVariantClear(&value);
            }
            if (SUCCEEDED(player->GetDuration(MFP_POSITIONTYPE_100NS, &value)))
            {
                // GetDuration은 VT_UI8로 답한다(GetPosition은 VT_I8). 한쪽만 읽으면 길이가 늘 0이다.
                if (value.vt == VT_UI8)
                    event.duration = std::chrono::milliseconds { static_cast<std::int64_t>(value.uhVal.QuadPart / 10000) };
                else if (value.vt == VT_I8)
                    event.duration = std::chrono::milliseconds { value.hVal.QuadPart / 10000 };
                (void)PropVariantClear(&value);
            }
            return event;
        }

        class media_callback final : public IMFPMediaPlayerCallback
        {
        public:
            explicit media_callback(std::shared_ptr<media_callbacks> state) : state_ { std::move(state) }
            {
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
            {
                if (object == nullptr)
                    return E_POINTER;
                *object = nullptr;
                if (iid != __uuidof(IUnknown) && iid != __uuidof(IMFPMediaPlayerCallback))
                    return E_NOINTERFACE;
                *object = static_cast<IMFPMediaPlayerCallback*>(this);
                AddRef();
                return S_OK;
            }
            ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
            ULONG STDMETHODCALLTYPE Release() override
            {
                const auto count { --references_ };
                if (count == 0)
                    delete this;
                return count;
            }
            void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* header) override
            {
                const std::scoped_lock lock { state_->gate };
                if (state_->stopped || header == nullptr)
                    return;
                HRESULT status { header->hrEvent };
                if (header->eEventType == MFP_EVENT_TYPE_MEDIAITEM_CREATED)
                {
                    const auto* created { reinterpret_cast<MFP_MEDIAITEM_CREATED_EVENT*>(header) };
                    if (created->dwUserData != state_->generation.load())
                        return;
                    if (SUCCEEDED(status))
                        status = header->pMediaPlayer->SetMediaItem(created->pMediaItem);
                    if (SUCCEEDED(status))
                        return;
                }
                if (header->eEventType == MFP_EVENT_TYPE_MEDIAITEM_SET && SUCCEEDED(status))
                {
                    const auto* item { reinterpret_cast<MFP_MEDIAITEM_SET_EVENT*>(header) };
                    ULONG_PTR generation {};
                    if (item->pMediaItem == nullptr || FAILED(item->pMediaItem->GetUserData(&generation)) || generation != state_->generation.load())
                        return;
                }
                if (FAILED(status))
                    state_->state = media_state::failed;
                else
                    switch (header->eEventType)
                    {
                    case MFP_EVENT_TYPE_MEDIAITEM_SET: state_->state = media_state::ready; break;
                    case MFP_EVENT_TYPE_PLAY: state_->state = media_state::playing; break;
                    case MFP_EVENT_TYPE_PAUSE: state_->state = media_state::paused; break;
                    case MFP_EVENT_TYPE_STOP: state_->state = media_state::stopped; break;
                    case MFP_EVENT_TYPE_PLAYBACK_ENDED: state_->state = media_state::ended; break;
                    default: return;
                    }
                try
                {
                    auto event { read_snapshot(header->pMediaPlayer, state_->state) };
                    if (FAILED(status))
                    {
                        event.native_error = status;
                        event.error = u8"Media Foundation could not load or play the source.";
                    }
                    if (state_->deliver != nullptr)
                        state_->deliver(std::move(event));
                }
                catch (...)
                {
                }
            }

        private:
            std::atomic<ULONG> references_ { 1 };
            std::shared_ptr<media_callbacks> state_ {};
        };
    }

    struct media_player::engine
    {
        ComPtr<IMFPMediaPlayer> player {};
        std::shared_ptr<media_callbacks> callbacks { std::make_shared<media_callbacks>() };
        HWND window { nullptr };
        DWORD thread { GetCurrentThreadId() };
        static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
        {
            auto* self { reinterpret_cast<engine*>(GetWindowLongPtrW(window, GWLP_USERDATA)) };
            if (message == WM_NCCREATE)
            {
                self = static_cast<engine*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
                (void)SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            if (message == WM_PAINT)
            {
                PAINTSTRUCT paint {};
                const HDC dc { BeginPaint(window, &paint) };
                (void)FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                (void)EndPaint(window, &paint);
                if (self != nullptr && self->player != nullptr)
                    (void)self->player->UpdateVideo();
                return 0;
            }
            if (message == WM_SIZE && self != nullptr && self->player != nullptr)
                (void)self->player->UpdateVideo();
            if (message == WM_ERASEBKGND)
                return 1;
            if (message == WM_NCDESTROY)
                (void)SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, wparam, lparam);
        }
    };

    media_player::media_player(std::unique_ptr<engine> engine) : engine_ { std::move(engine) }
    {
    }

    media_player::~media_player()
    {
        stop();
    }

    std::unique_ptr<media_player> media_player::create(media_player_config config, std::u8string& error)
    {
        auto engine { std::make_unique<media_player::engine>() };
        engine->callbacks->deliver = std::move(config.deliver);
        if (config.parent_window != nullptr)
        {
            constexpr auto class_name { L"Luil.Media.Surface" };
            WNDCLASSW window_class {};
            window_class.lpfnWndProc = media_player::engine::window_proc;
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpszClassName = class_name;
            if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                error = u8"Could not register the native video surface.";
                return nullptr;
            }
            engine->window = CreateWindowExW(0, class_name, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, static_cast<HWND>(config.parent_window), nullptr, GetModuleHandleW(nullptr), engine.get());
            if (engine->window == nullptr)
            {
                error = u8"Could not create the native video surface.";
                return nullptr;
            }
        }
        ComPtr<IMFPMediaPlayerCallback> callback {};
        callback.Attach(new media_callback { engine->callbacks });
        if (result(MFPCreateMediaPlayer(nullptr, FALSE, MFP_OPTION_FREE_THREADED_CALLBACK, callback.Get(), engine->window, &engine->player), error) == false)
        {
            if (engine->window != nullptr)
                (void)DestroyWindow(engine->window);
            return nullptr;
        }
        return std::unique_ptr<media_player> { new media_player { std::move(engine) } };
    }

    bool media_player::open(const std::u8string_view source, std::u8string& error)
    {
        if (source.empty() || source.find(char8_t {}) != std::u8string_view::npos || engine_->player == nullptr)
        {
            error = u8"Invalid media source or stopped player.";
            return false;
        }
        const auto wide { utf8_to_utf16(source) };
        if (wide.value.has_value() == false)
        {
            error = u8"Media source is not valid UTF-8.";
            return false;
        }
        const auto generation { ++engine_->callbacks->generation };
        {
            const std::scoped_lock lock { engine_->callbacks->gate };
            engine_->callbacks->state = media_state::opening;
        }
        return result(engine_->player->CreateMediaItemFromURL(wide.value->c_str(), FALSE, generation, nullptr), error);
    }

    bool media_player::play(std::u8string& error)
    {
        return result(engine_->player != nullptr ? engine_->player->Play() : MF_E_SHUTDOWN, error);
    }

    bool media_player::pause(std::u8string& error)
    {
        return result(engine_->player != nullptr ? engine_->player->Pause() : MF_E_SHUTDOWN, error);
    }

    bool media_player::rewind(std::u8string& error)
    {
        return result(engine_->player != nullptr ? engine_->player->Stop() : MF_E_SHUTDOWN, error) && seek(std::chrono::milliseconds { 0 }, error);
    }

    bool media_player::seek(const std::chrono::milliseconds position, std::u8string& error)
    {
        if (position.count() < 0 || position.count() > std::numeric_limits<LONGLONG>::max() / 10000 || engine_->player == nullptr)
        {
            error = u8"Invalid media seek position or stopped player.";
            return false;
        }
        PROPVARIANT value {};
        value.vt = VT_I8;
        value.hVal.QuadPart = position.count() * 10000;
        return result(engine_->player->SetPosition(MFP_POSITIONTYPE_100NS, &value), error);
    }

    bool media_player::volume(const float value, const bool muted, std::u8string& error)
    {
        if (std::isfinite(value) == false || value < 0.0f || value > 1.0f || engine_->player == nullptr)
        {
            error = u8"Invalid media volume or stopped player.";
            return false;
        }
        return result(engine_->player->SetVolume(value), error) && result(engine_->player->SetMute(muted), error);
    }

    void media_player::set_video_bounds(const rect_f bounds, const bool visible) noexcept
    {
        assert(GetCurrentThreadId() == engine_->thread);
        if (engine_->window == nullptr)
            return;
        if (std::isfinite(bounds.x) == false || std::isfinite(bounds.y) == false || std::isfinite(bounds.width) == false || std::isfinite(bounds.height) == false)
            return;
        const auto pixel = [](const float value) { return static_cast<int>(std::clamp(value, -1000000.0f, 1000000.0f)); };
        (void)SetWindowPos(engine_->window, HWND_TOP, pixel(bounds.x), pixel(bounds.y), pixel(std::max(0.0f, bounds.width)), pixel(std::max(0.0f, bounds.height)), SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        repaint();
    }

    void media_player::repaint() noexcept
    {
        if (engine_->player != nullptr)
            (void)engine_->player->UpdateVideo();
    }

    media_event media_player::snapshot() const
    {
        const std::scoped_lock lock { engine_->callbacks->gate };
        return engine_->player != nullptr ? read_snapshot(engine_->player.Get(), engine_->callbacks->state) : media_event {};
    }

    void media_player::stop() noexcept
    {
        assert(GetCurrentThreadId() == engine_->thread);
        {
            const std::scoped_lock lock { engine_->callbacks->gate };
            engine_->callbacks->stopped = true;
            engine_->callbacks->deliver = {};
        }
        if (engine_->player != nullptr)
        {
            (void)engine_->player->Shutdown();
            engine_->player.Reset();
        }
        if (engine_->window != nullptr)
        {
            (void)DestroyWindow(engine_->window);
            engine_->window = nullptr;
        }
    }
}
