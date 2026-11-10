#include "vk_loader.hpp"

namespace grace::render {
namespace vulkan {
namespace {

// The resolver for the loader-global entry points. The callback is whatever
// the backend was given: on Linux x86_64 the SDL_Vulkan_GetProcAddress
// family dispatches the call, on Windows and Android the loader-global
// entry points are callable without an instance handle.
bool resolve(void* (*resolveFn)(const char*), const char* name,
             void** target, std::string& missingSymbol)
{
    *target = resolveFn(name);
    if (*target == nullptr)
    {
        missingSymbol = name;
        return false;
    }
    return true;
}

// The resolver for the instance-bound entry points. The callback is the
// loader-global vkGetInstanceProcAddr bound to the instance handle.
bool resolveInstance(FnGetInstanceProcAddr getInstanceProcAddr,
                     VkInstance instance, const char* name, void** target,
                     std::string& missingSymbol)
{
    *target = getInstanceProcAddr(instance, name);
    if (*target == nullptr)
    {
        missingSymbol = name;
        return false;
    }
    return true;
}

// The resolver for the device-bound entry points. The callback is the
// instance's vkGetDeviceProcAddr bound to the device handle.
bool resolveDevice(FnGetDeviceProcAddr getDeviceProcAddr, VkDevice device,
                   const char* name, void** target,
                   std::string& missingSymbol)
{
    *target = getDeviceProcAddr(device, name);
    if (*target == nullptr)
    {
        missingSymbol = name;
        return false;
    }
    return true;
}

}  // namespace

bool VkLoader::load(ResolveFn resolveFn, std::string& missingSymbol)
{
    if (resolveFn == nullptr)
    {
        missingSymbol = "resolve callback";
        return false;
    }
    if (!resolve(reinterpret_cast<void* (*)(const char*)>(resolveFn),
                 "vkGetInstanceProcAddr",
                 reinterpret_cast<void**>(&vkGetInstanceProcAddr),
                 missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, nullptr,
                         "vkEnumerateInstanceExtensionProperties",
                         reinterpret_cast<void**>(
                             &vkEnumerateInstanceExtensionProperties),
                         missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, nullptr,
                         "vkCreateInstance",
                         reinterpret_cast<void**>(&vkCreateInstance),
                         missingSymbol))
    {
        return false;
    }
    return true;
}

bool VkLoader::loadInstance(ResolveFn resolveFn, VkInstance instance,
                            std::string& missingSymbol)
{
    if (resolveFn == nullptr)
    {
        missingSymbol = "resolve callback";
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
                 "vkDestroyInstance",
                 reinterpret_cast<void**>(&vkDestroyInstance),
                 missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
                 "vkEnumeratePhysicalDevices",
                 reinterpret_cast<void**>(&vkEnumeratePhysicalDevices),
                 missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
                 "vkGetPhysicalDeviceProperties",
                 reinterpret_cast<void**>(&vkGetPhysicalDeviceProperties),
                 missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
                 "vkGetPhysicalDeviceFeatures",
                 reinterpret_cast<void**>(&vkGetPhysicalDeviceFeatures),
                 missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
                 "vkGetPhysicalDeviceMemoryProperties",
                 reinterpret_cast<void**>(
                     &vkGetPhysicalDeviceMemoryProperties),
                 missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
            "vkGetPhysicalDeviceQueueFamilyProperties",
            reinterpret_cast<void**>(
                &vkGetPhysicalDeviceQueueFamilyProperties),
            missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
            "vkEnumerateDeviceExtensionProperties",
            reinterpret_cast<void**>(
                &vkEnumerateDeviceExtensionProperties),
            missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
                 "vkCreateDevice",
                 reinterpret_cast<void**>(&vkCreateDevice), missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
                 "vkGetDeviceProcAddr",
                 reinterpret_cast<void**>(&vkGetDeviceProcAddr),
                 missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
                 "vkDestroyDevice",
                 reinterpret_cast<void**>(&vkDestroyDevice), missingSymbol))
    {
        return false;
    }
    // vkCreateSurfaceKHR is deliberately not resolved: the Vulkan loader
    // dispatches it to the platform's VK_KHR_*_surface extension, and the
    // backend goes through SDL_Vulkan_CreateSurface instead (the same
    // window-system ownership rule the OpenGL backend follows).
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
                 "vkDestroySurfaceKHR",
                 reinterpret_cast<void**>(&vkDestroySurfaceKHR),
                 missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
            "vkGetPhysicalDeviceSurfaceSupportKHR",
            reinterpret_cast<void**>(
                &vkGetPhysicalDeviceSurfaceSupportKHR),
            missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
            reinterpret_cast<void**>(
                &vkGetPhysicalDeviceSurfaceCapabilitiesKHR),
            missingSymbol))
    {
        return false;
    }
    if (!resolveInstance(vkGetInstanceProcAddr, instance,
            "vkGetPhysicalDeviceSurfaceFormatsKHR",
            reinterpret_cast<void**>(
                &vkGetPhysicalDeviceSurfaceFormatsKHR),
            missingSymbol))
    {
        return false;
    }
    return true;
}

bool VkLoader::loadDevice(VkDevice device, std::string& missingSymbol)
{
    if (device == nullptr || vkGetDeviceProcAddr == nullptr)
    {
        missingSymbol = "vkGetDeviceProcAddr";
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkGetDeviceQueue",
                       reinterpret_cast<void**>(&vkGetDeviceQueue),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCreateBuffer",
                       reinterpret_cast<void**>(&vkCreateBuffer),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkDestroyBuffer",
                       reinterpret_cast<void**>(&vkDestroyBuffer),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkAllocateMemory",
                       reinterpret_cast<void**>(&vkAllocateMemory),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkFreeMemory",
                       reinterpret_cast<void**>(&vkFreeMemory),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkGetBufferMemoryRequirements",
                       reinterpret_cast<void**>(
                           &vkGetBufferMemoryRequirements),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkGetImageMemoryRequirements",
                       reinterpret_cast<void**>(
                           &vkGetImageMemoryRequirements),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkBindBufferMemory",
                       reinterpret_cast<void**>(&vkBindBufferMemory),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkBindImageMemory",
                       reinterpret_cast<void**>(&vkBindImageMemory),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkMapMemory",
                       reinterpret_cast<void**>(&vkMapMemory),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkUnmapMemory",
                       reinterpret_cast<void**>(&vkUnmapMemory),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkFlushMappedMemoryRanges",
                       reinterpret_cast<void**>(
                           &vkFlushMappedMemoryRanges),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCreateImage",
                       reinterpret_cast<void**>(&vkCreateImage),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkDestroyImage",
                       reinterpret_cast<void**>(&vkDestroyImage),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCreateImageView",
                       reinterpret_cast<void**>(&vkCreateImageView),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkDestroyImageView",
                       reinterpret_cast<void**>(&vkDestroyImageView),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCreateShaderModule",
                       reinterpret_cast<void**>(&vkCreateShaderModule),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkDestroyShaderModule",
                       reinterpret_cast<void**>(&vkDestroyShaderModule),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCreateCommandPool",
                       reinterpret_cast<void**>(&vkCreateCommandPool),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkDestroyCommandPool",
                       reinterpret_cast<void**>(&vkDestroyCommandPool),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkAllocateCommandBuffers",
                       reinterpret_cast<void**>(
                           &vkAllocateCommandBuffers),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkFreeCommandBuffers",
                       reinterpret_cast<void**>(&vkFreeCommandBuffers),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCreateFence",
                       reinterpret_cast<void**>(&vkCreateFence),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkDestroyFence",
                       reinterpret_cast<void**>(&vkDestroyFence),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCreateSemaphore",
                       reinterpret_cast<void**>(&vkCreateSemaphore),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkDestroySemaphore",
                       reinterpret_cast<void**>(&vkDestroySemaphore),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkWaitForFences",
                       reinterpret_cast<void**>(&vkWaitForFences),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkResetFences",
                       reinterpret_cast<void**>(&vkResetFences),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkBeginCommandBuffer",
                       reinterpret_cast<void**>(&vkBeginCommandBuffer),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkEndCommandBuffer",
                       reinterpret_cast<void**>(&vkEndCommandBuffer),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkResetCommandBuffer",
                       reinterpret_cast<void**>(&vkResetCommandBuffer),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkQueueSubmit",
                       reinterpret_cast<void**>(&vkQueueSubmit),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkQueueWaitIdle",
                       reinterpret_cast<void**>(&vkQueueWaitIdle),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkDeviceWaitIdle",
                       reinterpret_cast<void**>(&vkDeviceWaitIdle),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCmdBeginRenderPass",
                       reinterpret_cast<void**>(&vkCmdBeginRenderPass),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCmdEndRenderPass",
                       reinterpret_cast<void**>(&vkCmdEndRenderPass),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCmdSetViewport",
                       reinterpret_cast<void**>(&vkCmdSetViewport),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCmdSetScissor",
                       reinterpret_cast<void**>(&vkCmdSetScissor),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCmdDrawIndexed",
                       reinterpret_cast<void**>(&vkCmdDrawIndexed),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCmdDraw",
                       reinterpret_cast<void**>(&vkCmdDraw),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCmdBindPipeline",
                       reinterpret_cast<void**>(&vkCmdBindPipeline),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCmdBindVertexBuffers",
                       reinterpret_cast<void**>(&vkCmdBindVertexBuffers),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCmdBindIndexBuffer",
                       reinterpret_cast<void**>(&vkCmdBindIndexBuffer),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCmdBindDescriptorSets",
                       reinterpret_cast<void**>(
                           &vkCmdBindDescriptorSets),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCmdPushConstants",
                       reinterpret_cast<void**>(&vkCmdPushConstants),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCmdPipelineBarrier",
                       reinterpret_cast<void**>(&vkCmdPipelineBarrier),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCmdCopyBuffer",
                       reinterpret_cast<void**>(&vkCmdCopyBuffer),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCmdCopyBufferToImage",
                       reinterpret_cast<void**>(
                           &vkCmdCopyBufferToImage),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCmdCopyImage",
                       reinterpret_cast<void**>(&vkCmdCopyImage),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCmdCopyImageToBuffer",
                       reinterpret_cast<void**>(
                           &vkCmdCopyImageToBuffer),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCmdClearAttachments",
                       reinterpret_cast<void**>(&vkCmdClearAttachments),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCmdClearDepthStencilImage",
                       reinterpret_cast<void**>(
                           &vkCmdClearDepthStencilImage),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCmdFillBuffer",
                       reinterpret_cast<void**>(&vkCmdFillBuffer),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCreateRenderPass",
                       reinterpret_cast<void**>(&vkCreateRenderPass),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkDestroyRenderPass",
                       reinterpret_cast<void**>(&vkDestroyRenderPass),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCreateFramebuffer",
                       reinterpret_cast<void**>(&vkCreateFramebuffer),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkDestroyFramebuffer",
                       reinterpret_cast<void**>(&vkDestroyFramebuffer),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCreateGraphicsPipelines",
                       reinterpret_cast<void**>(
                           &vkCreateGraphicsPipelines),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkDestroyPipeline",
                       reinterpret_cast<void**>(&vkDestroyPipeline),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCreatePipelineLayout",
                       reinterpret_cast<void**>(
                           &vkCreatePipelineLayout),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkDestroyPipelineLayout",
                       reinterpret_cast<void**>(
                           &vkDestroyPipelineLayout),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCreateDescriptorSetLayout",
                       reinterpret_cast<void**>(
                           &vkCreateDescriptorSetLayout),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkDestroyDescriptorSetLayout",
                       reinterpret_cast<void**>(
                           &vkDestroyDescriptorSetLayout),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCreateDescriptorPool",
                       reinterpret_cast<void**>(
                           &vkCreateDescriptorPool),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkDestroyDescriptorPool",
                       reinterpret_cast<void**>(
                           &vkDestroyDescriptorPool),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkAllocateDescriptorSets",
                       reinterpret_cast<void**>(
                           &vkAllocateDescriptorSets),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkResetDescriptorPool",
                       reinterpret_cast<void**>(
                           &vkResetDescriptorPool),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkUpdateDescriptorSets",
                       reinterpret_cast<void**>(
                           &vkUpdateDescriptorSets),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkCreateSampler",
                       reinterpret_cast<void**>(&vkCreateSampler),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device, "vkDestroySampler",
                       reinterpret_cast<void**>(&vkDestroySampler),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkCreateSwapchainKHR",
                       reinterpret_cast<void**>(&vkCreateSwapchainKHR),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkDestroySwapchainKHR",
                       reinterpret_cast<void**>(&vkDestroySwapchainKHR),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkGetSwapchainImagesKHR",
                       reinterpret_cast<void**>(
                           &vkGetSwapchainImagesKHR),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkAcquireNextImageKHR",
                       reinterpret_cast<void**>(&vkAcquireNextImageKHR),
                       missingSymbol))
    {
        return false;
    }
    if (!resolveDevice(vkGetDeviceProcAddr, device,
                       "vkQueuePresentKHR",
                       reinterpret_cast<void**>(&vkQueuePresentKHR),
                       missingSymbol))
    {
        return false;
    }
    return true;
}

}  // namespace vulkan
}  // namespace grace::render
