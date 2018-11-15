#pragma once
#include <vector>

#include <vulkan/vulkan.hpp>
#include "Definitions.h"
#include "vma/vk_mem_alloc.h"
#include <GLFW/glfw3.h>

namespace vg
{
    class Context
    {
    public:
        Context(const std::vector<const char*>& requiredDeviceExtensions);
        ~Context();

        void initWindow();

        static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData);

        static std::vector<const char*> getRequiredExtensions();

        void initVulkan();

        void createAllocator();

        void createInstance();

        std::vector<vk::ExtensionProperties> getAllSupportedExtensions();

        bool checkValidationLayerSupport() const;

        void setupDebugCallback();

        void pickPhysicalDevice();

        bool isDeviceSuitable(vk::PhysicalDevice physDevice);

        bool checkDeviceExtensionSupport(vk::PhysicalDevice physDevice);

        QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice physDevice) const;

        void createLogicalDevice();

        void createSurface();

        SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice physDevice) const;

        static vk::SurfaceFormatKHR chooseSwapChainSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);

        static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes);

        vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);

        void createSwapChain();

        void createImageViews();

        vk::ShaderModule createShaderModule(const std::vector<char>& code) const;

        void initImgui();

        void cleanupImgui();

        GLFWwindow* getWindow() const { return m_window; }

        vk::Device getDevice() const { return m_device; }
        vk::PhysicalDevice getPhysicalDevice() const { return m_phsyicalDevice; }

        int getWidth() const { return m_width; }
        int getHeight() const { return m_height; }

        vk::Extent2D getSwapChainExtent() const { return m_swapChainExtent; }
        vk::Format getSwapChainImageFormat() const { return m_swapChainImageFormat; }
        std::vector<vk::ImageView> getSwapChainImageViews() const { return m_swapChainImageViews; }
        std::vector<vk::Image> getSwapChainImages() const { return m_swapChainImages; }
        vk::SwapchainKHR getSwapChain() const { return m_swapchain; }

        vk::Queue getGraphicsQueue() const { return m_graphicsQueue; }
        vk::Queue getPresentQueue() const { return m_presentQueue; }
        vk::Queue getTransferQueue() const { return m_transferQueue; }
        vk::Queue getComputeQueue() const { return m_computeQueue; }

        VmaAllocator getAllocator() const { return m_allocator; }

        const int max_frames_in_flight = 2;

        void setFrameBufferResized(const bool resized) { m_frameBufferResized = resized; }
        bool getFrameBufferResized() const { return m_frameBufferResized; }

        vk::RenderPass getImguiRenderpass() const { return m_imguiRenderpass; }

        vk::PhysicalDeviceRayTracingPropertiesNV getRaytracingProperties() { return m_raytracingProperties.value(); }

    private:
        // vk initialisation objects
        vk::Instance m_instance;
        vk::DebugUtilsMessengerEXT m_callback;
        vk::PhysicalDevice m_phsyicalDevice;
        vk::Device m_device;
        vk::Queue m_graphicsQueue;

        vk::Queue m_transferQueue;
        vk::Queue m_computeQueue;

        // presentation objects
        vk::SurfaceKHR m_surface;
        vk::Queue m_presentQueue;
        vk::SwapchainKHR m_swapchain;
        vk::Format m_swapChainImageFormat;
        vk::Extent2D m_swapChainExtent;
        std::vector<vk::Image> m_swapChainImages;
        std::vector<vk::ImageView> m_swapChainImageViews;

        VmaAllocator m_allocator = nullptr;

        GLFWwindow*  m_window;
        int m_width = 1600;
        int m_height = 900;
        bool m_frameBufferResized = false;

        // imgui objects
        vk::DescriptorPool m_imguiDescriptorPool;
        vk::RenderPass m_imguiRenderpass;

		// device extensions required by app
		std::vector<const char*> m_requiredDeviceExtensions;
		std::optional<vk::PhysicalDeviceRayTracingPropertiesNV> m_raytracingProperties;
    };
}


