#pragma once

#include "include/core/SkRefCnt.h"
#include "include/gpu/vk/VulkanExtensions.h"

#include <vulkan/vulkan_android.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>
#include <string>

class GrDirectContext;

namespace luil::android {
    // 렌더러가 쓰는 Vulkan 함수다. `libvulkan.so`를 dlopen해 얻는다 — NDK의 libvulkan을
    // 링크하지 않으므로 Vulkan이 없는 기기에서도 라이브러리가 열린다.
    struct vulkan_functions
    {
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR get_surface_support { nullptr };
        PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR get_surface_capabilities { nullptr };
        PFN_vkGetPhysicalDeviceSurfaceFormatsKHR get_surface_formats { nullptr };
        PFN_vkCreateAndroidSurfaceKHR create_android_surface { nullptr };
        PFN_vkDestroySurfaceKHR destroy_surface { nullptr };
        PFN_vkCreateSwapchainKHR create_swapchain { nullptr };
        PFN_vkDestroySwapchainKHR destroy_swapchain { nullptr };
        PFN_vkGetSwapchainImagesKHR get_swapchain_images { nullptr };
        PFN_vkAcquireNextImageKHR acquire_next_image { nullptr };
        PFN_vkQueuePresentKHR queue_present { nullptr };
        PFN_vkCreateSemaphore create_semaphore { nullptr };
        PFN_vkDestroySemaphore destroy_semaphore { nullptr };
        PFN_vkCreateFence create_fence { nullptr };
        PFN_vkDestroyFence destroy_fence { nullptr };
        PFN_vkWaitForFences wait_for_fences { nullptr };
        PFN_vkResetFences reset_fences { nullptr };
        PFN_vkQueueSubmit queue_submit { nullptr };
    };

    // Activity 하나가 쓰는 Vulkan 장치와 그 위의 Skia context다.
    //
    // **표면 소멸은 장치 소멸이 아니다.** 창은 회전·홈·화면 끄기마다 사라졌다 다시 생기지만
    // 이 객체는 그동안 남고, 창마다 스왑체인만 새로 선다 (vulkan_skia_renderer.h). 장치를
    // 잃으면(`lost`) 다시 쓰지 않는다 — 앱 host가 버리고 CPU로 물러선다.
    //  - UI thread에서만 쓴다. Skia context도 queue도 그 thread의 것이다.
    class vulkan_device
    {
    public:
        // 만들지 못하면 null이고 `error`에 이유가 남는다 (Vulkan이 없는 기기, 1.1 미만 등).
        static std::unique_ptr<vulkan_device> create(std::u8string& error);

        vulkan_device(const vulkan_device&) = delete;
        vulkan_device(vulkan_device&&) = delete;
        vulkan_device& operator=(const vulkan_device&) = delete;
        vulkan_device& operator=(vulkan_device&&) = delete;
        ~vulkan_device();

        [[nodiscard]] GrDirectContext& context() const noexcept;
        [[nodiscard]] const vulkan_functions& functions() const noexcept;
        [[nodiscard]] VkInstance instance() const noexcept;
        [[nodiscard]] VkPhysicalDevice physical_device() const noexcept;
        [[nodiscard]] VkDevice device() const noexcept;
        [[nodiscard]] VkQueue queue() const noexcept;
        [[nodiscard]] std::uint32_t queue_family() const noexcept;
        // 드라이버가 알리는 장치 이름이다 (`Adreno (TM) 730` 꼴). 로그에 남긴다.
        [[nodiscard]] const char* name() const noexcept;

        // 장치를 잃었는가. 잃은 장치에는 새 일을 시키지 않는다.
        [[nodiscard]] bool lost() const noexcept;
        void mark_lost() noexcept;
        // queue에 넣은 일이 다 끝날 때까지 기다린다. 예산(fence_wait.h)을 넘기거나 장치를
        // 잃으면 false다. 스왑체인을 버리기 전에 부른다.
        [[nodiscard]] bool wait_idle() noexcept;
        // fence 하나를 예산(fence_wait.h)까지 쪼개 기다린다. 신호가 오면 `VK_SUCCESS`, 예산을
        // 다 쓰면 `VK_TIMEOUT`(fence는 아직 쓰이는 중일 수 있다), 그 밖은 오류다. 장치 손실은
        // 기록한다.
        [[nodiscard]] VkResult wait_fence(VkFence fence) noexcept;
        // 그릴 표면이 없는 동안(백그라운드) GPU 자원을 돌려준다.
        void release_resources() noexcept;

    private:
        vulkan_device() = default;

        [[nodiscard]] bool initialize(std::u8string& error);
        [[nodiscard]] bool create_instance(std::u8string& error);
        [[nodiscard]] bool create_logical_device(std::u8string& error);
        [[nodiscard]] bool create_context(std::u8string& error);
        [[nodiscard]] bool load_functions(std::u8string& error);

        void* library_ { nullptr };
        PFN_vkGetInstanceProcAddr get_instance_proc_ { nullptr };
        PFN_vkGetDeviceProcAddr get_device_proc_ { nullptr };
        PFN_vkDestroyInstance destroy_instance_ { nullptr };
        PFN_vkDestroyDevice destroy_device_ { nullptr };
        VkInstance instance_ { VK_NULL_HANDLE };
        VkPhysicalDevice physical_device_ { VK_NULL_HANDLE };
        VkDevice device_ { VK_NULL_HANDLE };
        VkQueue queue_ { VK_NULL_HANDLE };
        std::uint32_t queue_family_ { 0 };
        std::uint32_t api_version_ { 0 };
        VkPhysicalDeviceProperties properties_ {};
        // 켠 기능이 없다는 것을 Skia에 알리려고 0으로 채운 것을 넘긴다.
        VkPhysicalDeviceFeatures features_ {};
        // Skia context가 사는 동안 남아 있어야 한다.
        skgpu::VulkanExtensions extensions_ {};
        vulkan_functions functions_ {};
        sk_sp<GrDirectContext> context_ {};
        bool lost_ { false };
    };
} // namespace luil::android
