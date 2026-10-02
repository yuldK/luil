#pragma once

// Skia의 Vulkan 메모리 할당기를 만드는 함수의 선언이다.
//
// M152의 Ganesh는 할당기를 스스로 만들지 않고 `VulkanBackendContext::fMemoryAllocator`로
// 받는다. 그것을 만드는 함수가 Skia 내부 헤더
// (src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h)에만 있어 패키지에 들지
// 않으므로, skia-prep의 tools/android_probe.cpp와 똑같이 여기서 선언한다.
//  - **Skia 내부 API다.** 핀의 `skia_milestone`을 올릴 때마다 서명을 다시 확인한다.
//    M152 패키지의 두 libskia.a(Debug·Release)에 이 서명의 기호가 있음을 확인했다
//    (`_ZN5skgpu22VulkanMemoryAllocators4MakeERKNS_20VulkanBackendContextENS_10ThreadSafeE`).
//  - `ThreadSafe`는 src/gpu/GpuTypesPriv.h에 있다 (kNo = false, kYes = true).

#include "include/core/SkRefCnt.h"

namespace skgpu {
    struct VulkanBackendContext;
    class VulkanMemoryAllocator;
    enum class ThreadSafe : bool;

    namespace VulkanMemoryAllocators {
        sk_sp<VulkanMemoryAllocator> Make(const VulkanBackendContext&, ThreadSafe);
    } // namespace VulkanMemoryAllocators
} // namespace skgpu
