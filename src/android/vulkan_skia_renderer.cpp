#include "android/vulkan_skia_renderer.h"

#include "android/vulkan_device.h"
#include "host/fail_fast.h"
#include "host/fence_wait.h"
#include "host/font_registry.h"
#include "luil/text/fonts.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"
#include "include/gpu/MutableTextureState.h"
#include "include/gpu/ganesh/GrBackendSemaphore.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/vk/GrVkBackendSemaphore.h"
#include "include/gpu/ganesh/vk/GrVkBackendSurface.h"
#include "include/gpu/ganesh/vk/GrVkTypes.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"

#include <android/log.h>
#include <android/native_window.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace luil::android {
    namespace {
        constexpr std::uint64_t nanoseconds_per_millisecond { 1'000'000 };

        // 스왑체인 이미지를 하나 받은 결과다.
        enum class acquire_result
        {
            acquired,
            // 이번 frame은 그리지 않는다. 창이 사라지는 중이라 곧 TERM_WINDOW가 온다.
            skipped,
            failed,
        };

        // 표면이 받는 형식 중 Skia가 그대로 쓰는 것을 고른다. CPU 렌더러와 같은 RGBA가 먼저다.
        struct surface_format
        {
            VkFormat format { VK_FORMAT_UNDEFINED };
            SkColorType color_type { kUnknown_SkColorType };
        };

        [[nodiscard]] surface_format choose_format(const std::vector<VkSurfaceFormatKHR>& formats) noexcept
        {
            for (const auto& [format, color_type] : { std::pair { VK_FORMAT_R8G8B8A8_UNORM, kRGBA_8888_SkColorType }, std::pair { VK_FORMAT_B8G8R8A8_UNORM, kBGRA_8888_SkColorType } })
                for (const VkSurfaceFormatKHR& candidate : formats)
                    if (candidate.format == format && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                        return { format, color_type };
            return {};
        }

        [[nodiscard]] VkCompositeAlphaFlagBitsKHR choose_composite_alpha(const VkCompositeAlphaFlagsKHR supported) noexcept
        {
            for (const VkCompositeAlphaFlagBitsKHR candidate : { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR })
                if ((supported & candidate) != 0)
                    return candidate;
            return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        }

        class vulkan_skia_renderer final : public skia_renderer
        {
        public:
            vulkan_skia_renderer(vulkan_device& device, ANativeWindow* const window)
                : device_ { &device }
                , functions_ { &device.functions() }
                , window_ { window }
                , codicon_typeface_ { load_codicon_typeface() }
            {
                ANativeWindow_acquire(window_);
            }

            vulkan_skia_renderer(const vulkan_skia_renderer&) = delete;
            vulkan_skia_renderer(vulkan_skia_renderer&&) = delete;
            vulkan_skia_renderer& operator=(const vulkan_skia_renderer&) = delete;
            vulkan_skia_renderer& operator=(vulkan_skia_renderer&&) = delete;

            ~vulkan_skia_renderer() override
            {
                const bool idle { device_->lost() == false && device_->wait_idle() };
                // 시간 초과는 장치 손실이 아니다. 실행 중인 자원을 부수거나 창을 CPU에 넘길 수 없다.
                if (idle == false && device_->lost() == false)
                {
                    __android_log_print(ANDROID_LOG_ERROR, "luil", "Vulkan shutdown timed out; pending resources cannot be destroyed safely");
                    platform_fail_fast();
                }
                release_images(idle);
                if (acquire_fence_ != VK_NULL_HANDLE)
                    functions_->destroy_fence(device_->device(), acquire_fence_, nullptr);
                if (swapchain_ != VK_NULL_HANDLE)
                    functions_->destroy_swapchain(device_->device(), swapchain_, nullptr);
                if (surface_ != VK_NULL_HANDLE)
                    functions_->destroy_surface(device_->instance(), surface_, nullptr);
                ANativeWindow_release(window_);
            }

            [[nodiscard]] bool initialize(std::u8string& error)
            {
                VkAndroidSurfaceCreateInfoKHR surface_info {};
                surface_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
                surface_info.window = window_;
                if (functions_->create_android_surface(device_->instance(), &surface_info, nullptr, &surface_) != VK_SUCCESS)
                {
                    surface_ = VK_NULL_HANDLE;
                    error = u8"Failed to create the Vulkan surface for the native window.";
                    return false;
                }

                VkBool32 presentable { VK_FALSE };
                if (functions_->get_surface_support(device_->physical_device(), device_->queue_family(), surface_, &presentable) != VK_SUCCESS || presentable == VK_FALSE)
                {
                    error = u8"The Vulkan graphics queue cannot present to the native window.";
                    return false;
                }

                std::uint32_t format_count { 0 };
                functions_->get_surface_formats(device_->physical_device(), surface_, &format_count, nullptr);
                std::vector<VkSurfaceFormatKHR> formats(format_count);
                functions_->get_surface_formats(device_->physical_device(), surface_, &format_count, formats.data());
                format_ = choose_format(formats);
                if (format_.format == VK_FORMAT_UNDEFINED)
                {
                    error = u8"The native window offers no RGBA or BGRA sRGB format for Vulkan.";
                    return false;
                }

                const VkFenceCreateInfo fence_info { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0 };
                if (functions_->create_fence(device_->device(), &fence_info, nullptr, &acquire_fence_) != VK_SUCCESS)
                {
                    acquire_fence_ = VK_NULL_HANDLE;
                    error = u8"Failed to create a Vulkan fence.";
                    return false;
                }

                width_ = std::max(1, ANativeWindow_getWidth(window_));
                height_ = std::max(1, ANativeWindow_getHeight(window_));
                return create_swapchain(error);
            }

            [[nodiscard]] renderer_backend backend() const noexcept override
            {
                return renderer_backend::vulkan;
            }

            [[nodiscard]] bool resize(const int width, const int height, std::u8string& error) override
            {
                const int safe_width { std::max(1, width) };
                const int safe_height { std::max(1, height) };
                if (safe_width == width_ && safe_height == height_ && needs_rebuild_ == false)
                    return true;
                width_ = safe_width;
                height_ = safe_height;
                return rebuild_swapchain(error);
            }

            [[nodiscard]] bool render(const frame_state& state, std::u8string& error) override
            {
                if (device_->lost())
                {
                    error = u8"The Vulkan device was lost.";
                    return false;
                }
                if (needs_rebuild_ && rebuild_swapchain(error) == false)
                    return false;

                std::uint32_t index { 0 };
                VkSemaphore acquired { VK_NULL_HANDLE };
                switch (acquire(index, acquired, error))
                {
                case acquire_result::failed:
                    return false;
                case acquire_result::skipped:
                    return true;
                case acquire_result::acquired:
                    break;
                }

                // 표시 엔진이 이미지를 놓아야 그리기 시작한다. 이 semaphore는 Skia가 맡아,
                // 기다린 제출이 끝난 뒤 지운다. semaphore가 없으면 CPU가 이미 기다린 것이다.
                SkSurface& surface { *surfaces_[index] };
                if (acquired != VK_NULL_HANDLE)
                {
                    const GrBackendSemaphore wait_semaphore { GrBackendSemaphores::MakeVk(acquired) };
                    if (surface.wait(1, &wait_semaphore) == false)
                    {
                        discard_acquire_semaphore(acquired);
                        error = u8"Skia could not wait for the acquired Vulkan image.";
                        return false;
                    }
                }

                // UI 글꼴은 설정이 정한다. registry가 cache하므로 매 frame 물어도 된다.
                const sk_sp<SkTypeface> ui { configured_ui_typeface() };
                draw_frame(*surface.getCanvas(), codicon_typeface_.get(), ui.get(), state);

                // 다 그리면 표시용 semaphore에 신호하고 이미지를 표시 배치로 옮긴다.
                GrBackendSemaphore signal_semaphore { GrBackendSemaphores::MakeVk(render_semaphores_[index]) };
                GrFlushInfo flush_info {};
                flush_info.fNumSemaphores = 1;
                flush_info.fSignalSemaphores = &signal_semaphore;
                const skgpu::MutableTextureState present_state { skgpu::MutableTextureStates::MakeVulkan(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, device_->queue_family()) };
                GrDirectContext& context { device_->context() };
                // semaphore가 제출되지 않으면 표시가 영영 기다리므로 그것도 실패로 친다.
                if (context.flush(&surface, flush_info, &present_state) != GrSemaphoresSubmitted::kYes || context.submit() == false)
                {
                    error = device_->lost() ? u8"The Vulkan device was lost while drawing." : u8"Skia failed to submit the Vulkan frame.";
                    return false;
                }
                return present(index, error);
            }

        private:
            // 받기의 신호가 아직 오지 않았을 수 있는 semaphore는 곧바로 지울 수 없다. 빈 제출이 그
            // 신호를 기다리게 하고, GPU가 그 제출까지 끝낸 뒤에 지운다. 장치를 잃었으면 기다릴 것이 없다.
            void discard_acquire_semaphore(const VkSemaphore semaphore) noexcept
            {
                if (device_->lost() == false)
                {
                    const VkPipelineStageFlags stage { VK_PIPELINE_STAGE_ALL_COMMANDS_BIT };
                    VkSubmitInfo submit {};
                    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                    submit.waitSemaphoreCount = 1;
                    submit.pWaitSemaphores = &semaphore;
                    submit.pWaitDstStageMask = &stage;
                    const VkResult result { functions_->queue_submit(device_->queue(), 1, &submit, VK_NULL_HANDLE) };
                    if (result == VK_ERROR_DEVICE_LOST)
                        device_->mark_lost();
                    // 제출하지 못했거나 그 제출의 끝을 확인하지 못했으면 아직 쓰는 중일 수 있다.
                    // 하나를 남기는 편이 잘못된 해제보다 낫다.
                    const bool finished { result == VK_SUCCESS && device_->wait_idle() };
                    if (finished == false && device_->lost() == false)
                        return;
                }
                functions_->destroy_semaphore(device_->device(), semaphore, nullptr);
            }

            [[nodiscard]] bool create_swapchain(std::u8string& error)
            {
                VkSurfaceCapabilitiesKHR capabilities {};
                if (functions_->get_surface_capabilities(device_->physical_device(), surface_, &capabilities) != VK_SUCCESS)
                {
                    error = u8"Failed to read the Vulkan surface capabilities.";
                    return false;
                }
                if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
                {
                    error = u8"The Vulkan surface cannot be rendered into.";
                    return false;
                }

                // 크기는 창의 논리 방향 그대로다. 회전은 컴포지터가 한다 (identity 전변환).
                //  - 표면의 전변환(`currentTransform`)에 맞추면 컴포지터의 회전이 빠지지만, 그만큼
                //    캔버스를 돌려 그려야 한다. 처음에는 identity로 맞게 세우고 측정해 정한다
                //    (docs/android-port-plan.md 4단계).
                const VkExtent2D extent {
                    std::clamp(static_cast<std::uint32_t>(width_), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                    std::clamp(static_cast<std::uint32_t>(height_), capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
                };
                pre_transform_ = (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0 ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;
                std::uint32_t image_count { std::max(capabilities.minImageCount, 2u) };
                if (capabilities.maxImageCount != 0)
                    image_count = std::min(image_count, capabilities.maxImageCount);
                // Skia가 복사·읽기에 이미지를 쓸 수 있게 Skia 자신의 창 예제와 같은 용도를 켠다.
                const VkImageUsageFlags usage {
                    capabilities.supportedUsageFlags & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT),
                };

                VkSwapchainCreateInfoKHR swapchain_info {};
                swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
                swapchain_info.surface = surface_;
                swapchain_info.minImageCount = image_count;
                swapchain_info.imageFormat = format_.format;
                swapchain_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                swapchain_info.imageExtent = extent;
                swapchain_info.imageArrayLayers = 1;
                swapchain_info.imageUsage = usage;
                swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
                swapchain_info.preTransform = pre_transform_;
                swapchain_info.compositeAlpha = choose_composite_alpha(capabilities.supportedCompositeAlpha);
                // 수직 동기다. 모든 구현이 받는 유일한 방식이기도 하다.
                swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
                swapchain_info.clipped = VK_TRUE;
                swapchain_info.oldSwapchain = swapchain_;
                VkSwapchainKHR created { VK_NULL_HANDLE };
                const VkResult result { functions_->create_swapchain(device_->device(), &swapchain_info, nullptr, &created) };
                // 옛 스왑체인은 새것을 만드는 데 넘긴 뒤에 버린다. 실패해도 이미 쓸 수 없다.
                if (swapchain_ != VK_NULL_HANDLE)
                    functions_->destroy_swapchain(device_->device(), swapchain_, nullptr);
                swapchain_ = VK_NULL_HANDLE;
                if (result != VK_SUCCESS)
                {
                    if (result == VK_ERROR_DEVICE_LOST)
                        device_->mark_lost();
                    error = u8"Failed to create the Vulkan swapchain.";
                    return false;
                }
                swapchain_ = created;
                extent_ = extent;
                suboptimal_checked_ = false;

                // 스왑체인이 설 때만 한 줄 남긴다. 기기마다 다른 이미지 수와 전변환을 logcat에서
                // 바로 맞대 보는 자리다.
                __android_log_print(ANDROID_LOG_INFO, "luil", "vulkan swapchain %ux%u, %u images (min %u), transform current %u pre %u", extent.width, extent.height, image_count,
                    capabilities.minImageCount, static_cast<unsigned>(capabilities.currentTransform), static_cast<unsigned>(pre_transform_));
                return wrap_images(usage, error);
            }

            [[nodiscard]] bool wrap_images(const VkImageUsageFlags usage, std::u8string& error)
            {
                std::uint32_t count { 0 };
                functions_->get_swapchain_images(device_->device(), swapchain_, &count, nullptr);
                std::vector<VkImage> images(count);
                if (count == 0 || functions_->get_swapchain_images(device_->device(), swapchain_, &count, images.data()) != VK_SUCCESS)
                {
                    error = u8"Failed to read the Vulkan swapchain images.";
                    return false;
                }

                // 화면의 서브픽셀 배치를 모르므로(회전도 한다) 회색조로 그린다. CPU 렌더러와 같다.
                const SkSurfaceProps surface_properties { 0, kUnknown_SkPixelGeometry };
                const VkSemaphoreCreateInfo semaphore_info { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
                for (const VkImage image : images)
                {
                    GrVkImageInfo image_info {};
                    image_info.fImage = image;
                    image_info.fImageTiling = VK_IMAGE_TILING_OPTIMAL;
                    image_info.fImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    image_info.fFormat = format_.format;
                    image_info.fImageUsageFlags = usage;
                    image_info.fSampleCount = 1;
                    image_info.fLevelCount = 1;
                    image_info.fCurrentQueueFamily = device_->queue_family();
                    image_info.fSharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    const GrBackendRenderTarget render_target {
                        GrBackendRenderTargets::MakeVk(static_cast<int>(extent_.width), static_cast<int>(extent_.height), image_info),
                    };
                    sk_sp<SkSurface> surface {
                        SkSurfaces::WrapBackendRenderTarget(&device_->context(), render_target, kTopLeft_GrSurfaceOrigin, format_.color_type, SkColorSpace::MakeSRGB(), &surface_properties),
                    };
                    if (surface == nullptr)
                    {
                        error = u8"Skia failed to wrap a Vulkan swapchain image as a surface.";
                        return false;
                    }
                    surfaces_.push_back(std::move(surface));

                    // 표시용 semaphore는 이미지마다 하나다. 같은 이미지를 다시 받았다면 그 이미지의
                    // 앞선 표시가 끝난 것이라 그때 다시 써도 된다.
                    VkSemaphore semaphore { VK_NULL_HANDLE };
                    if (functions_->create_semaphore(device_->device(), &semaphore_info, nullptr, &semaphore) != VK_SUCCESS)
                    {
                        error = u8"Failed to create a Vulkan semaphore.";
                        return false;
                    }
                    render_semaphores_.push_back(semaphore);
                }
                image_used_.assign(images.size(), false);
                unused_images_ = images.size();
                return true;
            }

            // 감싼 표면과 표시용 semaphore를 놓는다. 스왑체인을 버리거나 다시 세우기 전에 부른다.
            //  - `idle`이면 GPU가 일을 다 끝낸 뒤다. Skia가 쥔 명령 버퍼의 참조까지 거두도록
            //    한 번 더 제출한다 — 그래야 이미지의 view·framebuffer가 스왑체인보다 먼저 사라진다.
            void release_images(const bool idle) noexcept
            {
                surfaces_.clear();
                if (idle)
                    device_->context().submit(GrSyncCpu::kYes);
                for (const VkSemaphore semaphore : render_semaphores_)
                    functions_->destroy_semaphore(device_->device(), semaphore, nullptr);
                render_semaphores_.clear();
                image_used_.clear();
                unused_images_ = 0;
            }

            [[nodiscard]] bool rebuild_swapchain(std::u8string& error)
            {
                if (device_->wait_idle() == false)
                {
                    error = device_->lost() ? u8"The Vulkan device was lost before rebuilding the swapchain." : u8"Failed to wait for the GPU before rebuilding the Vulkan swapchain.";
                    return false;
                }
                release_images(true);
                // 이미지까지 다 감쌀 때까지 낡은 것으로 둔다. 도중에 실패하면 스왑체인이 없거나 이미지가
                // 덜 감싸여 있어, 다음 frame이 그대로 받으면 null 스왑체인이나 빈 배열을 쓴다.
                needs_rebuild_ = true;
                if (create_swapchain(error) == false)
                    return false;
                needs_rebuild_ = false;
                return true;
            }

            // 스왑체인 이미지 하나를 받는다. `semaphore`는 그 이미지를 표시 엔진이 놓을 때 신호를
            // 받는다. CPU가 이미 기다렸으면 null이다.
            //
            // **아직 한 번도 그리지 않은 이미지가 남아 있으면 fence로 받아 CPU에서 기다린다.**
            // 새 이미지의 레이아웃은 UNDEFINED이고, Skia는 그 전환 장벽의 시작 단계를
            // TOP_OF_PIPE로 둔다. 그러면 semaphore를 기다리는 단계와 이어지지 않아, 표시 엔진이
            // 아직 읽는 이미지에 전환이 쓸 수 있다 (동기화 검증의 SYNC-HAZARD-WRITE-AFTER-READ).
            // 한 번 그린 이미지는 PRESENT_SRC에서 바뀌므로 semaphore로 충분하다. 스왑체인이 설 때
            // 처음 몇 frame만 CPU가 기다린다.
            [[nodiscard]] acquire_result acquire(std::uint32_t& index, VkSemaphore& semaphore, std::u8string& error)
            {
                const VkSemaphoreCreateInfo semaphore_info { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
                // 낡은 스왑체인이면 한 번 다시 세워 다시 받는다.
                for (int attempt { 0 }; attempt < 2; ++attempt)
                {
                    const bool host_wait { unused_images_ > 0 };
                    semaphore = VK_NULL_HANDLE;
                    if (host_wait && acquire_fence_ == VK_NULL_HANDLE)
                    {
                        error = u8"The Vulkan acquire fence is no longer usable.";
                        return acquire_result::failed;
                    }
                    if (host_wait == false && functions_->create_semaphore(device_->device(), &semaphore_info, nullptr, &semaphore) != VK_SUCCESS)
                    {
                        semaphore = VK_NULL_HANDLE;
                        error = u8"Failed to create a Vulkan semaphore.";
                        return acquire_result::failed;
                    }

                    // FIFO라 보통 바로 돌아온다. 그래도 상한 없이 막히지 않게 예산 안에서 쪼개 기다린다
                    // (fence_wait.h).
                    VkResult result { VK_TIMEOUT };
                    for (std::uint32_t waited_ms { 0 };;)
                    {
                        const std::uint32_t slice_ms { next_fence_wait_slice(waited_ms) };
                        if (slice_ms == 0)
                            break;
                        result = functions_->acquire_next_image(device_->device(), swapchain_, slice_ms * nanoseconds_per_millisecond, semaphore, host_wait ? acquire_fence_ : VK_NULL_HANDLE, &index);
                        if (result != VK_TIMEOUT && result != VK_NOT_READY)
                            break;
                        waited_ms += slice_ms;
                    }

                    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
                    {
                        if (result == VK_SUBOPTIMAL_KHR)
                            note_suboptimal();
                        if (host_wait && wait_acquire_fence(error) == false)
                            return acquire_result::failed;
                        if (image_used_[index] == false)
                        {
                            image_used_[index] = true;
                            --unused_images_;
                        }
                        return acquire_result::acquired;
                    }
                    if (semaphore != VK_NULL_HANDLE)
                        functions_->destroy_semaphore(device_->device(), semaphore, nullptr);
                    semaphore = VK_NULL_HANDLE;
                    if (result == VK_ERROR_OUT_OF_DATE_KHR)
                    {
                        if (rebuild_swapchain(error) == false)
                            return acquire_result::failed;
                        continue;
                    }
                    if (result == VK_ERROR_SURFACE_LOST_KHR)
                        return acquire_result::skipped;
                    if (result == VK_ERROR_DEVICE_LOST)
                        device_->mark_lost();
                    error = result == VK_TIMEOUT ? u8"Timed out acquiring a Vulkan swapchain image." : u8"Failed to acquire a Vulkan swapchain image.";
                    return acquire_result::failed;
                }
                // 다시 세운 것도 곧바로 낡았다. 회전 도중의 경합이라 이번 frame만 거른다.
                needs_rebuild_ = true;
                return acquire_result::skipped;
            }

            [[nodiscard]] bool wait_acquire_fence(std::u8string& error)
            {
                const VkResult result { device_->wait_fence(acquire_fence_) };
                if (result == VK_SUCCESS)
                {
                    functions_->reset_fences(device_->device(), 1, &acquire_fence_);
                    return true;
                }
                // 이 fence는 표시 엔진의 일이다. queue의 빈 제출을 기다려도 완료를 증명할 수 없다.
                if (device_->lost() == false)
                {
                    __android_log_print(ANDROID_LOG_ERROR, "luil", "Vulkan image acquisition did not complete; the swapchain cannot be destroyed safely");
                    platform_fail_fast();
                }
                error = result == VK_TIMEOUT ? u8"Timed out waiting for a Vulkan swapchain image." : u8"Failed to wait for a Vulkan swapchain image.";
                return false;
            }

            [[nodiscard]] bool present(const std::uint32_t index, std::u8string& error)
            {
                VkPresentInfoKHR present_info {};
                present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                present_info.waitSemaphoreCount = 1;
                present_info.pWaitSemaphores = &render_semaphores_[index];
                present_info.swapchainCount = 1;
                present_info.pSwapchains = &swapchain_;
                present_info.pImageIndices = &index;
                const VkResult result { functions_->queue_present(device_->queue(), &present_info) };
                switch (result)
                {
                case VK_SUCCESS:
                    return true;
                case VK_SUBOPTIMAL_KHR:
                    note_suboptimal();
                    return true;
                case VK_ERROR_OUT_OF_DATE_KHR:
                    // 이 frame은 화면에 닿지 않았을 수 있다. 크기가 바뀐 것이라 host의 resize와
                    // 다시 그리기가 곧 따라온다. 다음 frame 앞에서 다시 세운다.
                    needs_rebuild_ = true;
                    return true;
                case VK_ERROR_SURFACE_LOST_KHR:
                    return true;
                case VK_ERROR_DEVICE_LOST:
                    device_->mark_lost();
                    error = u8"The Vulkan device was lost while presenting.";
                    return false;
                default:
                    error = u8"Failed to present the Vulkan frame.";
                    return false;
                }
            }

            // 스왑체인이 "최적이 아니다"고 알렸다.
            // identity 전변환을 회전된 표면에 쓰면 Android는 frame마다 이것을 돌려준다. 그때마다
            // 다시 세우면 끝없이 다시 세우므로, 전변환이 어긋난 탓이면 그대로 쓴다. 그 밖의
            // 까닭이면 다음 frame 앞에서 다시 세운다. 스왑체인 하나에 한 번만 묻는다.
            void note_suboptimal() noexcept
            {
                if (suboptimal_checked_)
                    return;
                suboptimal_checked_ = true;
                VkSurfaceCapabilitiesKHR capabilities {};
                if (functions_->get_surface_capabilities(device_->physical_device(), surface_, &capabilities) != VK_SUCCESS)
                    return;
                // 크기가 바뀐 것은 host의 resize가 따로 알린다.
                if (capabilities.currentTransform == pre_transform_)
                    needs_rebuild_ = true;
            }

            vulkan_device* device_;
            const vulkan_functions* functions_;
            ANativeWindow* window_;
            sk_sp<SkTypeface> codicon_typeface_ {};
            VkSurfaceKHR surface_ { VK_NULL_HANDLE };
            VkSwapchainKHR swapchain_ { VK_NULL_HANDLE };
            surface_format format_ {};
            VkExtent2D extent_ {};
            VkSurfaceTransformFlagBitsKHR pre_transform_ { VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR };
            int width_ { 1 };
            int height_ { 1 };
            std::vector<sk_sp<SkSurface>> surfaces_ {};
            std::vector<VkSemaphore> render_semaphores_ {};
            // 처음 쓸 이미지를 CPU에서 기다리는 데 쓴다 (`acquire`).
            VkFence acquire_fence_ { VK_NULL_HANDLE };
            // 이미지마다 한 번이라도 그렸는가와 아직 그리지 않은 수다.
            std::vector<bool> image_used_ {};
            std::size_t unused_images_ { 0 };
            bool needs_rebuild_ { false };
            bool suboptimal_checked_ { false };
        };
    } // namespace

    renderer_factory_result create_vulkan_skia_renderer(vulkan_device& device, ANativeWindow* const window)
    {
        if (window == nullptr)
            return { nullptr, u8"There is no native window to render into." };
        if (device.lost())
            return { nullptr, u8"The Vulkan device was lost." };

        auto renderer { std::make_unique<vulkan_skia_renderer>(device, window) };
        std::u8string error {};
        if (renderer->initialize(error) == false)
            return { nullptr, std::move(error) };
        return { std::move(renderer), {} };
    }
} // namespace luil::android
