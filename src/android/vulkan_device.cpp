#include "android/vulkan_device.h"

#include "android/vulkan_memory_allocator.h"
#include "host/fence_wait.h"

#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanMemoryAllocator.h"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <vector>

namespace luil::android {
    namespace {
        // Skia가 요구하는 하한이다 (VulkanBackendContext.h).
        constexpr std::uint32_t required_api_version { VK_API_VERSION_1_1 };
        constexpr std::array<const char*, 2> instance_extensions { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME };
        constexpr std::array<const char*, 1> device_extensions { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        constexpr std::uint64_t nanoseconds_per_millisecond { 1'000'000 };

        template<typename function_type>
        [[nodiscard]] function_type instance_function(const PFN_vkGetInstanceProcAddr get, const VkInstance instance, const char* const name) noexcept
        {
            return reinterpret_cast<function_type>(get(instance, name));
        }

        template<typename function_type>
        [[nodiscard]] function_type device_function(const PFN_vkGetDeviceProcAddr get, const VkDevice device, const char* const name) noexcept
        {
            return reinterpret_cast<function_type>(get(device, name));
        }

        [[nodiscard]] bool has_extension(const std::vector<VkExtensionProperties>& available, const std::string_view name) noexcept
        {
            return std::ranges::any_of(available, [name](const VkExtensionProperties& properties) { return name == properties.extensionName; });
        }

        [[nodiscard]] std::u8string missing_extension_error(const std::u8string_view what, const char* const name)
        {
            std::u8string error { what };
            error += reinterpret_cast<const char8_t*>(name);
            error += u8'.';
            return error;
        }
    } // namespace

    std::unique_ptr<vulkan_device> vulkan_device::create(std::u8string& error)
    {
        std::unique_ptr<vulkan_device> device { new vulkan_device {} };
        if (device->initialize(error) == false)
            return nullptr;
        return device;
    }

    vulkan_device::~vulkan_device()
    {
        if (context_ != nullptr)
        {
            // 대기가 실패해도(예산 소진·장치 손실) 그대로 진행한다. 잃은 장치에는 abandon만
            // 하고, 아니면 Skia가 쥔 자원을 먼저 놓은 뒤 장치를 부순다.
            if (lost() == false && wait_idle())
                context_->releaseResourcesAndAbandonContext();
            else
                context_->abandonContext();
            context_.reset();
        }
        if (device_ != VK_NULL_HANDLE && destroy_device_ != nullptr)
            destroy_device_(device_, nullptr);
        if (instance_ != VK_NULL_HANDLE && destroy_instance_ != nullptr)
            destroy_instance_(instance_, nullptr);
        if (library_ != nullptr)
            dlclose(library_);
    }

    bool vulkan_device::initialize(std::u8string& error)
    {
        library_ = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (library_ == nullptr)
        {
            error = u8"libvulkan.so is not available.";
            return false;
        }
        get_instance_proc_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library_, "vkGetInstanceProcAddr"));
        if (get_instance_proc_ == nullptr)
        {
            error = u8"libvulkan.so has no vkGetInstanceProcAddr.";
            return false;
        }
        return create_instance(error) && create_logical_device(error) && load_functions(error) && create_context(error);
    }

    bool vulkan_device::create_instance(std::u8string& error)
    {
        const auto enumerate_version { instance_function<PFN_vkEnumerateInstanceVersion>(get_instance_proc_, VK_NULL_HANDLE, "vkEnumerateInstanceVersion") };
        std::uint32_t instance_version { VK_API_VERSION_1_0 };
        if (enumerate_version != nullptr)
            enumerate_version(&instance_version);
        if (instance_version < required_api_version)
        {
            error = u8"The Vulkan loader is older than 1.1.";
            return false;
        }
        api_version_ = required_api_version;

        const auto enumerate_extensions {
            instance_function<PFN_vkEnumerateInstanceExtensionProperties>(get_instance_proc_, VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"),
        };
        std::uint32_t extension_count { 0 };
        enumerate_extensions(nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> available(extension_count);
        enumerate_extensions(nullptr, &extension_count, available.data());
        for (const char* const name : instance_extensions)
            if (has_extension(available, name) == false)
            {
                error = missing_extension_error(u8"The Vulkan loader lacks ", name);
                return false;
            }

        VkApplicationInfo application {};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pEngineName = "luil";
        application.apiVersion = api_version_;
        VkInstanceCreateInfo instance_info {};
        instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_info.pApplicationInfo = &application;
        instance_info.enabledExtensionCount = static_cast<std::uint32_t>(instance_extensions.size());
        instance_info.ppEnabledExtensionNames = instance_extensions.data();
        const auto create { instance_function<PFN_vkCreateInstance>(get_instance_proc_, VK_NULL_HANDLE, "vkCreateInstance") };
        if (create(&instance_info, nullptr, &instance_) != VK_SUCCESS)
        {
            instance_ = VK_NULL_HANDLE;
            error = u8"vkCreateInstance failed.";
            return false;
        }
        destroy_instance_ = instance_function<PFN_vkDestroyInstance>(get_instance_proc_, instance_, "vkDestroyInstance");
        get_device_proc_ = instance_function<PFN_vkGetDeviceProcAddr>(get_instance_proc_, instance_, "vkGetDeviceProcAddr");
        return true;
    }

    bool vulkan_device::create_logical_device(std::u8string& error)
    {
        const auto enumerate_devices { instance_function<PFN_vkEnumeratePhysicalDevices>(get_instance_proc_, instance_, "vkEnumeratePhysicalDevices") };
        std::uint32_t device_count { 0 };
        enumerate_devices(instance_, &device_count, nullptr);
        if (device_count == 0)
        {
            error = u8"There is no Vulkan physical device.";
            return false;
        }
        std::vector<VkPhysicalDevice> devices(device_count);
        enumerate_devices(instance_, &device_count, devices.data());
        // 휴대폰에는 GPU가 하나다. 첫 장치를 쓴다.
        physical_device_ = devices.front();

        const auto get_properties { instance_function<PFN_vkGetPhysicalDeviceProperties>(get_instance_proc_, instance_, "vkGetPhysicalDeviceProperties") };
        get_properties(physical_device_, &properties_);
        if (properties_.apiVersion < required_api_version)
        {
            error = u8"The Vulkan device is older than 1.1.";
            return false;
        }

        const auto enumerate_extensions {
            instance_function<PFN_vkEnumerateDeviceExtensionProperties>(get_instance_proc_, instance_, "vkEnumerateDeviceExtensionProperties"),
        };
        std::uint32_t extension_count { 0 };
        enumerate_extensions(physical_device_, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> available(extension_count);
        enumerate_extensions(physical_device_, nullptr, &extension_count, available.data());
        for (const char* const name : device_extensions)
            if (has_extension(available, name) == false)
            {
                error = missing_extension_error(u8"The Vulkan device lacks ", name);
                return false;
            }

        const auto get_queue_families {
            instance_function<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(get_instance_proc_, instance_, "vkGetPhysicalDeviceQueueFamilyProperties"),
        };
        std::uint32_t family_count { 0 };
        get_queue_families(physical_device_, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        get_queue_families(physical_device_, &family_count, families.data());
        const auto graphics {
            std::ranges::find_if(families, [](const VkQueueFamilyProperties& family) { return (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0; }),
        };
        if (graphics == families.end())
        {
            error = u8"The Vulkan device has no graphics queue.";
            return false;
        }
        // 표시도 이 queue가 한다. 표시를 받는지는 표면이 생겨야 물을 수 있어 렌더러가 확인한다.
        queue_family_ = static_cast<std::uint32_t>(graphics - families.begin());

        const float priority { 1.0f };
        VkDeviceQueueCreateInfo queue_info {};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family_;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_info {};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions.size());
        device_info.ppEnabledExtensionNames = device_extensions.data();
        device_info.pEnabledFeatures = &features_;
        const auto create { instance_function<PFN_vkCreateDevice>(get_instance_proc_, instance_, "vkCreateDevice") };
        if (create(physical_device_, &device_info, nullptr, &device_) != VK_SUCCESS)
        {
            device_ = VK_NULL_HANDLE;
            error = u8"vkCreateDevice failed.";
            return false;
        }
        destroy_device_ = device_function<PFN_vkDestroyDevice>(get_device_proc_, device_, "vkDestroyDevice");
        const auto get_queue { device_function<PFN_vkGetDeviceQueue>(get_device_proc_, device_, "vkGetDeviceQueue") };
        get_queue(device_, queue_family_, 0, &queue_);
        return true;
    }

    bool vulkan_device::load_functions(std::u8string& error)
    {
        const PFN_vkGetInstanceProcAddr get_instance { get_instance_proc_ };
        const PFN_vkGetDeviceProcAddr get_device { get_device_proc_ };
        vulkan_functions& f { functions_ };
        f.get_surface_support = instance_function<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(get_instance, instance_, "vkGetPhysicalDeviceSurfaceSupportKHR");
        f.get_surface_capabilities = instance_function<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(get_instance, instance_, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        f.get_surface_formats = instance_function<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(get_instance, instance_, "vkGetPhysicalDeviceSurfaceFormatsKHR");
        f.create_android_surface = instance_function<PFN_vkCreateAndroidSurfaceKHR>(get_instance, instance_, "vkCreateAndroidSurfaceKHR");
        f.destroy_surface = instance_function<PFN_vkDestroySurfaceKHR>(get_instance, instance_, "vkDestroySurfaceKHR");
        f.create_swapchain = device_function<PFN_vkCreateSwapchainKHR>(get_device, device_, "vkCreateSwapchainKHR");
        f.destroy_swapchain = device_function<PFN_vkDestroySwapchainKHR>(get_device, device_, "vkDestroySwapchainKHR");
        f.get_swapchain_images = device_function<PFN_vkGetSwapchainImagesKHR>(get_device, device_, "vkGetSwapchainImagesKHR");
        f.acquire_next_image = device_function<PFN_vkAcquireNextImageKHR>(get_device, device_, "vkAcquireNextImageKHR");
        f.queue_present = device_function<PFN_vkQueuePresentKHR>(get_device, device_, "vkQueuePresentKHR");
        f.create_semaphore = device_function<PFN_vkCreateSemaphore>(get_device, device_, "vkCreateSemaphore");
        f.destroy_semaphore = device_function<PFN_vkDestroySemaphore>(get_device, device_, "vkDestroySemaphore");
        f.create_fence = device_function<PFN_vkCreateFence>(get_device, device_, "vkCreateFence");
        f.destroy_fence = device_function<PFN_vkDestroyFence>(get_device, device_, "vkDestroyFence");
        f.wait_for_fences = device_function<PFN_vkWaitForFences>(get_device, device_, "vkWaitForFences");
        f.reset_fences = device_function<PFN_vkResetFences>(get_device, device_, "vkResetFences");
        f.queue_submit = device_function<PFN_vkQueueSubmit>(get_device, device_, "vkQueueSubmit");

        const bool complete {
            f.get_surface_support != nullptr && f.get_surface_capabilities != nullptr && f.get_surface_formats != nullptr && f.create_android_surface != nullptr && f.destroy_surface != nullptr
                && f.create_swapchain != nullptr && f.destroy_swapchain != nullptr && f.get_swapchain_images != nullptr && f.acquire_next_image != nullptr && f.queue_present != nullptr
                && f.create_semaphore != nullptr && f.destroy_semaphore != nullptr && f.create_fence != nullptr && f.destroy_fence != nullptr && f.wait_for_fences != nullptr
                && f.reset_fences != nullptr && f.queue_submit != nullptr,
        };
        if (complete == false)
        {
            error = u8"The Vulkan driver does not export every function the renderer needs.";
            return false;
        }
        return true;
    }

    bool vulkan_device::create_context(std::u8string& error)
    {
        const PFN_vkGetInstanceProcAddr get_instance { get_instance_proc_ };
        const PFN_vkGetDeviceProcAddr get_device { get_device_proc_ };
        const auto get_proc = [get_instance, get_device](const char* const name, const VkInstance instance, const VkDevice device) -> PFN_vkVoidFunction {
            if (device != VK_NULL_HANDLE)
                return get_device(device, name);
            return get_instance(instance, name);
        };
        // Skia에는 실제로 켠 확장을 알린다. 켜지 않은 것을 알리면 그 확장의 함수를 찾다 실패한다.
        extensions_.init(get_proc, instance_, physical_device_, static_cast<std::uint32_t>(instance_extensions.size()), instance_extensions.data(),
            static_cast<std::uint32_t>(device_extensions.size()), device_extensions.data());

        skgpu::VulkanBackendContext backend {};
        backend.fInstance = instance_;
        backend.fPhysicalDevice = physical_device_;
        backend.fDevice = device_;
        backend.fQueue = queue_;
        backend.fGraphicsQueueIndex = queue_family_;
        backend.fMaxAPIVersion = api_version_;
        backend.fVkExtensions = &extensions_;
        backend.fDeviceFeatures = &features_;
        backend.fGetProc = get_proc;
        // context는 UI thread 하나에서만 쓴다.
        backend.fMemoryAllocator = skgpu::VulkanMemoryAllocators::Make(backend, static_cast<skgpu::ThreadSafe>(false));
        if (backend.fMemoryAllocator == nullptr)
        {
            error = u8"Failed to create the Vulkan memory allocator.";
            return false;
        }
        context_ = GrDirectContexts::MakeVulkan(backend);
        if (context_ == nullptr)
        {
            error = u8"Failed to create the Skia Vulkan context.";
            return false;
        }
        return true;
    }

    GrDirectContext& vulkan_device::context() const noexcept
    {
        return *context_;
    }

    const vulkan_functions& vulkan_device::functions() const noexcept
    {
        return functions_;
    }

    VkInstance vulkan_device::instance() const noexcept
    {
        return instance_;
    }

    VkPhysicalDevice vulkan_device::physical_device() const noexcept
    {
        return physical_device_;
    }

    VkDevice vulkan_device::device() const noexcept
    {
        return device_;
    }

    VkQueue vulkan_device::queue() const noexcept
    {
        return queue_;
    }

    std::uint32_t vulkan_device::queue_family() const noexcept
    {
        return queue_family_;
    }

    const char* vulkan_device::name() const noexcept
    {
        return properties_.deviceName;
    }

    bool vulkan_device::lost() const noexcept
    {
        return lost_ || (context_ != nullptr && context_->isDeviceLost());
    }

    void vulkan_device::mark_lost() noexcept
    {
        lost_ = true;
    }

    bool vulkan_device::wait_idle() noexcept
    {
        if (lost())
            return false;
        // Skia가 아직 내지 않은 일까지 queue에 넣은 뒤, 빈 제출에 fence를 걸어 기다린다.
        // fence의 신호는 그보다 먼저 제출된 일이 모두 끝난 뒤에 온다.
        //  - `vkDeviceWaitIdle`·`vkQueueWaitIdle`은 상한이 없다. 드라이버가 끝내 돌아오지
        //    않으면 CPU 물러섬을 포함한 복구 전체가 닿지 못하므로 예산을 둔다 (fence_wait.h).
        context_->flush();
        context_->submit();
        VkFenceCreateInfo fence_info {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence { VK_NULL_HANDLE };
        if (functions_.create_fence(device_, &fence_info, nullptr, &fence) != VK_SUCCESS)
            return false;
        if (const VkResult submitted { functions_.queue_submit(queue_, 0, nullptr, fence) }; submitted != VK_SUCCESS)
        {
            if (submitted == VK_ERROR_DEVICE_LOST)
                mark_lost();
            functions_.destroy_fence(device_, fence, nullptr);
            return false;
        }

        const VkResult result { wait_fence(fence) };
        // 예산을 다 쓴 fence는 아직 queue가 쥐고 있을 수 있어 부수지 않는다.
        // 여기에 닿은 것이 이미 비정상이고, 하나를 남기는 편이 잘못된 해제보다 낫다.
        if (result != VK_TIMEOUT)
            functions_.destroy_fence(device_, fence, nullptr);
        return result == VK_SUCCESS;
    }

    VkResult vulkan_device::wait_fence(const VkFence fence) noexcept
    {
        for (std::uint32_t waited_ms { 0 };;)
        {
            const std::uint32_t slice_ms { next_fence_wait_slice(waited_ms) };
            if (slice_ms == 0)
                return VK_TIMEOUT;
            const VkResult result { functions_.wait_for_fences(device_, 1, &fence, VK_TRUE, slice_ms * nanoseconds_per_millisecond) };
            if (result != VK_TIMEOUT)
            {
                if (result == VK_ERROR_DEVICE_LOST)
                    mark_lost();
                return result;
            }
            waited_ms += slice_ms;
        }
    }

    void vulkan_device::release_resources() noexcept
    {
        if (context_ == nullptr || lost())
            return;
        context_->freeGpuResources();
    }
} // namespace luil::android
