#include <vulkan/vulkan.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // use Vulkans depth range [0, 1]
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"

#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <filesystem>

const std::vector<const char*> validationLayers = { "VK_LAYER_LUNARG_standard_validation" };
const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

#ifdef NDEBUG
    constexpr bool enableValidationLayers = false;
#else
    constexpr bool enableValidationLayers = true;
#endif

struct Vertex
{
    glm::vec2 pos;
    glm::vec3 color;

    static vk::VertexInputBindingDescription getBindingDescription()
    {
        vk::VertexInputBindingDescription desc(0, sizeof(Vertex), vk::VertexInputRate::eVertex);
        return desc;
    }

    static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
    {
        std::array<vk::VertexInputAttributeDescription, 2> attributeDescriptions;
        attributeDescriptions.at(0).binding = 0;
        attributeDescriptions.at(0).location = 0;
        attributeDescriptions.at(0).format = vk::Format::eR32G32Sfloat;
        attributeDescriptions.at(0).offset = offsetof(Vertex, pos);

        attributeDescriptions.at(1).binding = 0;
        attributeDescriptions.at(1).location = 1;
        attributeDescriptions.at(1).format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions.at(1).offset = offsetof(Vertex, color);

        return attributeDescriptions;
    }
};

const std::vector<Vertex> vertices = {
    {{0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}},
    {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
};

namespace help
{
    VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pCallback) {
        auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, pCallback);
        }
        else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT callback, const VkAllocationCallbacks* pAllocator) {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (func != nullptr) {
            func(instance, callback, pAllocator);
        }
    }
}

namespace vg
{
    class Context
    {
    public:
        Context(const Context& context) = delete;

        Context()
        {
            initWindow();
            initVulkan();
        }

        ~Context()
        {
            vmaDestroyAllocator(m_allocator);

            for (const auto& imageView : m_swapChainImageViews)
            {
                m_device.destroyImageView(imageView);
            }
            m_device.destroySwapchainKHR(m_swapchain);
            m_instance.destroySurfaceKHR(m_surface);
            
            m_device.destroy();
            help::DestroyDebugUtilsMessengerEXT(m_instance, m_callback, nullptr);

            m_instance.destroy();

            glfwDestroyWindow(m_window);
            glfwTerminate();
        }

        void initWindow()
        {
            glfwInit();
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            //glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            m_window = glfwCreateWindow(m_width, m_height, "Vulkan", nullptr, nullptr);
            glfwSetWindowUserPointer(m_window, this);
            glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);

        }

        static void framebufferResizeCallback(GLFWwindow* window, int width, int height)
        {
            auto context = reinterpret_cast<Context*>(glfwGetWindowUserPointer(window));
            context->m_frameBufferResized = true;
        }

        void initVulkan()
        {
            createInstance();
            setupDebugCallback();

            createSurface();

            pickPhysicalDevice();
            createLogicalDevice();

            createAllocator();

            createSwapChain();
            createImageViews();
        }

        void createAllocator()
        {
            VmaAllocatorCreateInfo createInfo = {};
            createInfo.device = m_device;
            createInfo.physicalDevice = m_phsyicalDevice;
            //createInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
            auto result = vmaCreateAllocator(&createInfo, &m_allocator);

            if (result != VK_SUCCESS)
                throw std::runtime_error("Failed to create Allocator");
        }

        void createInstance()
        {
            if (enableValidationLayers && !checkValidationLayerSupport())
            {
                throw std::runtime_error("Validation layers requested, but not available!");
            }

            vk::ApplicationInfo appInfo("Vulkan Test New", 1, "No Engine", 1, VK_API_VERSION_1_0);

            // glfw + (cond.) debug layer
            auto requiredExtensions = getRequiredExtensions();

            std::vector<const char*> layerNames;

            if constexpr (enableValidationLayers)
                layerNames = validationLayers;

            vk::InstanceCreateInfo createInfo({}, &appInfo, static_cast<uint32_t>(layerNames.size()), layerNames.data(), static_cast<uint32_t>(requiredExtensions.size()), requiredExtensions.data());


            if(vk::createInstance(&createInfo, nullptr, &m_instance) != vk::Result::eSuccess)
                throw std::runtime_error("Instance could not be created");

            // print instance extensions
            // getAllSupportedExtensions(true);
        }

        // get glfw + (cond.) debug layer extensions
        std::vector<const char*> getRequiredExtensions()
        {
            uint32_t glfwExtensionCount = 0;
            auto exts = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
            std::vector<const char*> extensions(exts, exts + glfwExtensionCount);

            if constexpr (enableValidationLayers)
            {
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                //extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
            }

            return extensions;
        }

        std::vector<vk::ExtensionProperties> getAllSupportedExtensions(bool printExtensions = false)
        {
            // this is the place to look for raytracing extension support        
            std::vector<vk::ExtensionProperties> extensions = vk::enumerateInstanceExtensionProperties();

            if(printExtensions)
            {
                std::cout << "Available extensions:\n";
                for (const auto& extension : extensions)
                {
                    std::cout << '\t' << extension.extensionName << '\n';
                }
            }

            return extensions;

        }

        bool checkValidationLayerSupport()
        {
            uint32_t layerCount;
            vk::enumerateInstanceLayerProperties(&layerCount, nullptr);

            std::vector<vk::LayerProperties> availableLayers(layerCount);
            vk::enumerateInstanceLayerProperties(&layerCount, availableLayers.data());

            for (const char* layerName : validationLayers)
            {
                bool layerFound = false;

                for (const auto& layerProperties : availableLayers)
                {
                    if (strcmp(layerName, layerProperties.layerName) == 0)
                    {
                        layerFound = true;
                        break;
                    }
                }

                if (!layerFound)
                {
                    return false;
                }
            }

            return true;
        }

        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData)
        {

            std::cout << "Validation layer says: " << pCallbackData->pMessage << std::endl;

            return VK_FALSE;
        }

        void setupDebugCallback()
        {
            if constexpr (!enableValidationLayers) return;

            using sevFlags = vk::DebugUtilsMessageSeverityFlagBitsEXT;
            using typeFlags = vk::DebugUtilsMessageTypeFlagBitsEXT;

            vk::DebugUtilsMessengerCreateInfoEXT createInfo(
                {},
                sevFlags::eError | sevFlags::eWarning | sevFlags::eVerbose,// | sevFlags::eInfo,
                typeFlags::eGeneral | typeFlags::ePerformance | typeFlags::eValidation,
                reinterpret_cast<PFN_vkDebugUtilsMessengerCallbackEXT>(debugCallback));


            if (help::CreateDebugUtilsMessengerEXT(m_instance,
                reinterpret_cast<VkDebugUtilsMessengerCreateInfoEXT*>(&createInfo), nullptr,
                reinterpret_cast<VkDebugUtilsMessengerEXT *>(&m_callback)) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to set up debug callback!");
            }
        }

        void pickPhysicalDevice()
        {
            // TODO scoring system preferring discrete GPUs
            auto physDevices = m_instance.enumeratePhysicalDevices();

            if (physDevices.empty())
                throw std::runtime_error("No physical devices found");

            for(const auto& device: physDevices)
            {
                if (isDeviceSuitable(device))
                {
                    m_phsyicalDevice = device;
                    break;
                }
            }

            if(!m_phsyicalDevice)
                throw std::runtime_error("No suitable physical device found");


        }

        struct QueueFamilyIndices
        {
            std::optional<uint32_t> graphicsFamily;
            std::optional<uint32_t> presentFamily;
            std::optional<uint32_t> transferFamily;
            std::optional<uint32_t> computeFamily;

            bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value()
                                      && transferFamily.has_value() && computeFamily.has_value(); }
        };

        bool isDeviceSuitable(vk::PhysicalDevice physDevice)
        {
            // TODO use actual stuff here
            auto properties = physDevice.getProperties();
            auto features = physDevice.getFeatures();

            //device.getProperties().vendorID NVIDIA only?

            // look for a GPU with geometry shader
            const bool suitable = (properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu ||
                            properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
                            && features.geometryShader;

            // look for a graphics queue
            QueueFamilyIndices indices = findQueueFamilies(physDevice);

            // look if the wanted extensions are supported
            const bool extensionSupport = checkDeviceExtensionSupport(physDevice);

            // look for swapchain support
            bool swapChainAdequate = false;
            if (extensionSupport)
            {
                SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physDevice);
                swapChainAdequate = !swapChainSupport.m_formats.empty() && !swapChainSupport.m_presentModes.empty();
            }

            return suitable && indices.isComplete() && extensionSupport && swapChainAdequate;
        }

        bool checkDeviceExtensionSupport(vk::PhysicalDevice physDevice)
        {
            auto availableExtensions = physDevice.enumerateDeviceExtensionProperties();

            std::set<std::string> requiredExtensions;
            for(const auto& extension : availableExtensions)
                requiredExtensions.emplace(extension.extensionName);

            for (const auto& extension : availableExtensions)
                requiredExtensions.erase(extension.extensionName);

            return requiredExtensions.empty();
        }

        QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice physDevice)
        {
            QueueFamilyIndices indices;
            auto qfprops = physDevice.getQueueFamilyProperties();

            uint32_t i = 0;
            for (const auto& queueFamily : qfprops)
            {
                if (queueFamily.queueCount > 0 && queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
                    indices.graphicsFamily = i;

                bool presentSupport = physDevice.getSurfaceSupportKHR(i, m_surface);
                if (queueFamily.queueCount > 0 && presentSupport)
                    indices.presentFamily = i;

                if (queueFamily.queueCount > 0 && queueFamily.queueFlags & vk::QueueFlagBits::eTransfer && !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
                    indices.transferFamily = i;

                if (queueFamily.queueCount > 0 && queueFamily.queueFlags & vk::QueueFlagBits::eCompute && !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
                    indices.computeFamily = i;

                if (indices.isComplete())
                    break;

                i++;
            }

            return indices;
        }

        // create logical device and get graphics and present queue
        void createLogicalDevice()
        {
            QueueFamilyIndices indices = findQueueFamilies(m_phsyicalDevice);

            std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
            std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value(), indices.transferFamily.value(), indices.computeFamily.value() };

            float queuePriority = 1.0f;
            for (uint32_t queueFamily : uniqueQueueFamilies)
            {
                vk::DeviceQueueCreateInfo queueCreateInfo({}, queueFamily, 1, &queuePriority);
                queueCreateInfos.push_back(queueCreateInfo);
            }

            vk::PhysicalDeviceFeatures deviceFeatures;

            vk::DeviceCreateInfo createInfo({},
                static_cast<uint32_t>(queueCreateInfos.size()), queueCreateInfos.data(),
                0, nullptr,
                static_cast<uint32_t>(deviceExtensions.size()), deviceExtensions.data(), &deviceFeatures);

            if constexpr(enableValidationLayers)
            {
                createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
                createInfo.ppEnabledLayerNames = validationLayers.data();
            }

            m_device = m_phsyicalDevice.createDevice(createInfo);

            m_presentQueue = m_device.getQueue(indices.presentFamily.value(), 0);
            m_graphicsQueue = m_device.getQueue(indices.graphicsFamily.value(), 0);
            m_transferQueue = m_device.getQueue(indices.transferFamily.value(), 0);
            m_computeQueue = m_device.getQueue(indices.computeFamily.value(), 0);

        }

        void createSurface()
        {
            if (glfwCreateWindowSurface(m_instance, m_window, nullptr, reinterpret_cast<VkSurfaceKHR*>(&m_surface)) != VK_SUCCESS)
                throw std::runtime_error("Surface creation failed");
        }

        struct SwapChainSupportDetails
        {
            vk::SurfaceCapabilitiesKHR m_capabilities;
            std::vector<vk::SurfaceFormatKHR> m_formats;
            std::vector<vk::PresentModeKHR> m_presentModes;
        };

        SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice physDevice)
        {
            SwapChainSupportDetails details;
            details.m_capabilities  = physDevice.getSurfaceCapabilitiesKHR(m_surface);
            details.m_formats       = physDevice.getSurfaceFormatsKHR(m_surface);
            details.m_presentModes  = physDevice.getSurfacePresentModesKHR(m_surface);

            return details;
        }

        vk::SurfaceFormatKHR chooseSwapChainSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
        {
            if (availableFormats.size() == 1 && availableFormats.at(0).format == vk::Format::eUndefined)
                return { vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear };

            for(const auto& availableFormat : availableFormats)
            {
                if (availableFormat.format == vk::Format::eB8G8R8A8Unorm && availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
                    return availableFormat;
            }

            return availableFormats.at(0);
        }

        vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes)
        {
            auto bestMode = vk::PresentModeKHR::eFifo;

            for (const auto& availablePresentMode : availablePresentModes)
            {
                if (availablePresentMode == vk::PresentModeKHR::eMailbox)
                    return availablePresentMode;
                if (availablePresentMode == vk::PresentModeKHR::eImmediate)
                    bestMode = availablePresentMode;
            }

            return bestMode;
        }

        vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities)
        {
            if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            {
                return capabilities.currentExtent;
            }
            else 
            {
                glfwGetFramebufferSize(m_window, &m_width, &m_height);
                VkExtent2D actualExtent = { static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height) };
                actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
                actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));
                return actualExtent;
            }
        }

        void createSwapChain()
        {
            auto swapChainSupport = querySwapChainSupport(m_phsyicalDevice);

            auto surfaceFormat = chooseSwapChainSurfaceFormat(swapChainSupport.m_formats);
            auto presentMode = chooseSwapPresentMode(swapChainSupport.m_presentModes);
            auto extent = chooseSwapExtent(swapChainSupport.m_capabilities);

            // determine the number of images in the swapchain (queue length)
            uint32_t imageCount = swapChainSupport.m_capabilities.minImageCount + 1;
            if (swapChainSupport.m_capabilities.maxImageCount > 0 && imageCount > swapChainSupport.m_capabilities.maxImageCount)
                imageCount = swapChainSupport.m_capabilities.maxImageCount;

            vk::SwapchainCreateInfoKHR createInfo({}, m_surface, imageCount, surfaceFormat.format, surfaceFormat.colorSpace, extent, 1, vk::ImageUsageFlagBits::eColorAttachment);

            QueueFamilyIndices indices = findQueueFamilies(m_phsyicalDevice);
            uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

            if(indices.graphicsFamily != indices.presentFamily)
            {
                createInfo.imageSharingMode = vk::SharingMode::eConcurrent; // exclusive is standard value
                createInfo.queueFamilyIndexCount = 2;                       // 0 is standard value
            }
            else
            {
                createInfo.imageSharingMode = vk::SharingMode::eExclusive;
                createInfo.queueFamilyIndexCount = 1;
            }
            createInfo.pQueueFamilyIndices = queueFamilyIndices;

            createInfo.preTransform = swapChainSupport.m_capabilities.currentTransform;
            createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
            createInfo.presentMode = presentMode;
            createInfo.clipped = true;
            createInfo.oldSwapchain = nullptr;

            m_swapchain = m_device.createSwapchainKHR(createInfo);
            m_swapChainImages = m_device.getSwapchainImagesKHR(m_swapchain);

            m_swapChainImageFormat = surfaceFormat.format;
            m_swapChainExtent = extent;
        }

        void createImageViews()
        {
            m_swapChainImageViews.resize(m_swapChainImages.size());
            for (size_t i = 0; i < m_swapChainImages.size(); i++)
            {
                vk::ImageSubresourceRange subresRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
                vk::ImageViewCreateInfo createInfo({}, m_swapChainImages.at(i), vk::ImageViewType::e2D, m_swapChainImageFormat, {}, subresRange);
                m_swapChainImageViews.at(i) = m_device.createImageView(createInfo);
            }
        }

        vk::ShaderModule createShaderModule(const std::vector<char>& code)
        {
            vk::ShaderModuleCreateInfo createInfo({}, code.size(), reinterpret_cast<const uint32_t*>(code.data()));
            return m_device.createShaderModule(createInfo);
        }

        GLFWwindow* getWindow() const { return m_window; }

        vk::Device getDevice() const { return m_device; }
        vk::PhysicalDevice getPhysicalDevice() const { return m_phsyicalDevice; }

        int getWidth() const { return m_width; }
        int getHeight() const { return m_height; }

        vk::Extent2D getSwapChainExtent() const { return m_swapChainExtent; }
        vk::Format getSwapChainImageFormat() const { return m_swapChainImageFormat; }
        std::vector<vk::ImageView> getSwapChainImageViews() const { return m_swapChainImageViews; }
        vk::SwapchainKHR getSwapChain() const { return m_swapchain; }

        vk::Queue getGraphicsQueue() const { return m_graphicsQueue; }
        vk::Queue getPresentQueue() const { return m_presentQueue;  }
        vk::Queue getTransferQueue() const { return m_transferQueue; }
        vk::Queue getComputeQueue() const { return m_computeQueue; }

        VmaAllocator getAllocator()
        {
            return m_allocator; 
        }

        const int max_frames_in_flight = 3;

        void setFrameBufferResized(bool resized) { m_frameBufferResized = resized; }
        bool getFrameBufferResized() const { return m_frameBufferResized; }

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

    };

}

class Utility
{
public:
    static std::vector<char> readFile(const std::filesystem::path& filePath)
    {
        std::ifstream file(s_shaderPath / filePath, std::ios::ate | std::ios::binary);

        if (!file.is_open())
            throw std::runtime_error(std::string("Failed to open file") + filePath.filename().string());

        auto fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);

        file.close();

        return buffer;
    }
private:
    inline static const auto s_shaderPath = std::filesystem::current_path().parent_path().parent_path().append("shaders");
};


class App
{
public:
    App()
    {
        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandPools();

        createVertexBuffer();

        createCommandBuffers();
        createSyncObjects();
    }

    ~App()
    {
        vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_vertexBufferInfo.m_Buffer), m_vertexBufferInfo.m_BufferAllocation);

        for (int i = 0; i < m_context.max_frames_in_flight; i++)
        {
            m_context.getDevice().destroySemaphore(m_imageAvailableSemaphores.at(i));
            m_context.getDevice().destroySemaphore(m_renderFinishedSemaphores.at(i));
            m_context.getDevice().destroyFence(m_inFlightFences.at(i));
        }

        m_context.getDevice().destroyCommandPool(m_commandPool);
        m_context.getDevice().destroyCommandPool(m_transferCommandPool);
        m_context.getDevice().destroyCommandPool(m_computeCommandPool);

        for (const auto framebuffer : m_swapChainFramebuffers)
            m_context.getDevice().destroyFramebuffer(framebuffer);

        m_context.getDevice().destroyPipeline(m_graphicsPipeline);
        m_context.getDevice().destroyPipelineLayout(m_pipelineLayout);
        m_context.getDevice().destroyRenderPass(m_renderpass);
        // cleanup here
    }

    // base
    struct BufferInfo
    {
        vk::Buffer m_Buffer = nullptr;
        VmaAllocation m_BufferAllocation = nullptr;
        VmaAllocationInfo m_BufferAllocInfo = {};
    };

    // base 
    BufferInfo createBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags& usage, const VmaMemoryUsage properties, vk::SharingMode sharingMode = vk::SharingMode::eExclusive)
    {
        BufferInfo bufferInfo;

        vk::BufferCreateInfo bufferCreateInfo({}, size, usage, sharingMode);
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = properties;

        const auto result = vmaCreateBuffer(m_context.getAllocator(),
            reinterpret_cast<VkBufferCreateInfo*>(&bufferCreateInfo), &allocInfo, reinterpret_cast<VkBuffer*>(&bufferInfo.m_Buffer), &bufferInfo.m_BufferAllocation, &bufferInfo.m_BufferAllocInfo);

        if (result != VK_SUCCESS)
            throw std::runtime_error("Buffer creation failed");

        return bufferInfo;
    }

    // base
    void copyBuffer(const vk::Buffer src, const vk::Buffer dst, const vk::DeviceSize size)
    {
        vk::CommandBufferAllocateInfo allocInfo(m_transferCommandPool, vk::CommandBufferLevel::ePrimary, 1);

        auto commandBuffer = m_context.getDevice().allocateCommandBuffers(allocInfo).at(0);

        vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

        vk::BufferCopy copyRegion(0, 0, size);

        commandBuffer.copyBuffer(src, dst, copyRegion);
        commandBuffer.end();

        vk::SubmitInfo submitInfo;
        submitInfo.setCommandBufferCount(1);
        submitInfo.setPCommandBuffers(&commandBuffer);

        m_context.getTransferQueue().submit(submitInfo, nullptr);
        m_context.getTransferQueue().waitIdle();

        m_context.getDevice().freeCommandBuffers(m_transferCommandPool, 1, &commandBuffer);
    }

    template <typename T>
    BufferInfo fillBufferTroughStagedTransfer(const std::vector<T>& data, const vk::BufferUsageFlags actualBufferUsage)
    {
        vk::DeviceSize bufferSize = sizeof(T) * data.size();

        auto stagingBufferInfo = createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_TO_GPU, vk::SharingMode::eConcurrent);

        void* dataPtr = m_context.getDevice().mapMemory(stagingBufferInfo.m_BufferAllocInfo.deviceMemory, stagingBufferInfo.m_BufferAllocInfo.offset, stagingBufferInfo.m_BufferAllocInfo.size, {});
        memcpy(dataPtr, vertices.data(), stagingBufferInfo.m_BufferAllocInfo.size);
        m_context.getDevice().unmapMemory(stagingBufferInfo.m_BufferAllocInfo.deviceMemory);

        auto returnBufferInfo = createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | actualBufferUsage, VMA_MEMORY_USAGE_GPU_ONLY, vk::SharingMode::eConcurrent);

        copyBuffer(stagingBufferInfo.m_Buffer, returnBufferInfo.m_Buffer, bufferSize);

        vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(returnBufferInfo.m_Buffer), returnBufferInfo.m_BufferAllocation);

        return returnBufferInfo;
    }

    void createVertexBuffer()
    {
        m_vertexBufferInfo = fillBufferTroughStagedTransfer(vertices, vk::BufferUsageFlagBits::eVertexBuffer);
    }


    // base
    void recreateSwapChain()
    {
        int width = 0, height = 0;
        while (width == 0 || height == 0)
        {
            glfwGetFramebufferSize(m_context.getWindow(), &width, &height);
            glfwWaitEvents();
        }

        m_context.getDevice().waitIdle();

        cleanUpSwapchain();

        m_context.createSwapChain();
        m_context.createImageViews();
        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandBuffers();
    }

    // base
    void cleanUpSwapchain()
    {
        for (auto& scfb : m_swapChainFramebuffers)
            m_context.getDevice().destroyFramebuffer(scfb);

        m_context.getDevice().freeCommandBuffers(m_commandPool, m_commandBuffers);

        m_context.getDevice().destroyPipeline(m_graphicsPipeline);
        m_context.getDevice().destroyPipelineLayout(m_pipelineLayout);
        m_context.getDevice().destroyRenderPass(m_renderpass);

        for (auto& sciv : m_context.getSwapChainImageViews())
            m_context.getDevice().destroyImageView(sciv);

        m_context.getDevice().destroySwapchainKHR(m_context.getSwapChain());
    }

    void createGraphicsPipeline()
    {
        const auto vertShaderCode = Utility::readFile("new/shader.vert.spv");
        const auto fragShaderCode = Utility::readFile("new/shader.frag.spv");

        const auto vertShaderModule = m_context.createShaderModule(vertShaderCode);
        const auto fragShaderModule = m_context.createShaderModule(fragShaderCode);

        const vk::PipelineShaderStageCreateInfo vertShaderStageInfo({}, vk::ShaderStageFlagBits::eVertex, vertShaderModule, "main");
        const vk::PipelineShaderStageCreateInfo fragShaderStageInfo({}, vk::ShaderStageFlagBits::eFragment, fragShaderModule, "main");

        const vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };


        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo({}, 1, &bindingDescription, static_cast<uint32_t>(attributeDescriptions.size()), attributeDescriptions.data());

               

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList, VK_FALSE);

        vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(m_context.getWidth()), static_cast<float>(m_context.getHeight()), 0.0f, 1.0f);

        vk::Rect2D scissor({ 0, 0 }, m_context.getSwapChainExtent());

        vk::PipelineViewportStateCreateInfo viewportState({}, 1, &viewport, 1, &scissor);

        vk::PipelineRasterizationStateCreateInfo rasterizer({}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise, VK_FALSE, 0, 0, 0, 1.0f);

        vk::PipelineMultisampleStateCreateInfo mulitsampling({}, vk::SampleCountFlagBits::e1, VK_FALSE);

        vk::PipelineColorBlendAttachmentState colorBlendAttachment; // standard values for blending.
        colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
        colorBlendAttachment.setBlendEnable(VK_FALSE);
        // to enable blending, translate the following into hpp code: (and also use logic op copy in the struct below)
        //colorBlendAttachment.blendEnable = VK_TRUE;
        //colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        //colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        //colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        //colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        //colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        //colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        vk::PipelineColorBlendStateCreateInfo colorBlending; // standard values for now
        colorBlending.setLogicOpEnable(VK_FALSE);
        colorBlending.setLogicOp(vk::LogicOp::eCopy);
        colorBlending.attachmentCount = 1;
        colorBlending.setPAttachments(&colorBlendAttachment);
        colorBlending.setBlendConstants(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});

        //std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };

        //vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates.size(), dynamicStates.data());

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
        pipelineLayoutInfo.setSetLayoutCount(0);
        pipelineLayoutInfo.setPushConstantRangeCount(0);


        m_pipelineLayout = m_context.getDevice().createPipelineLayout(pipelineLayoutInfo);
        
        vk::GraphicsPipelineCreateInfo pipelineInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &mulitsampling;
        pipelineInfo.pDepthStencilState = nullptr;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = nullptr;// &dynamicState;

        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = m_renderpass;
        pipelineInfo.subpass = 0;   // this is an index
        // missing: pipeline derivation
        
        m_graphicsPipeline = m_context.getDevice().createGraphicsPipeline(nullptr, pipelineInfo);

        m_context.getDevice().destroyShaderModule(vertShaderModule);
        m_context.getDevice().destroyShaderModule(fragShaderModule);
    }

    void createRenderPass()
    {
        vk::AttachmentDescription colorAttachment({}, m_context.getSwapChainImageFormat(),
            vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,            // load store op
            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,      // stencil op
            vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR
        );

        vk::AttachmentReference colorAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal);

        vk::SubpassDependency dependency(VK_SUBPASS_EXTERNAL, 0,
            vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
            {}, vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite, vk::DependencyFlagBits::eByRegion);


        vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics,
            0, nullptr,                 // input attachments (standard values)
            1, &colorAttachmentRef);    // color attachments: layout (location = 0) out -> colorAttachmentRef is at index 0
                                        // other attachment at standard values: Resolve, DepthStencil, Preserved

        vk::RenderPassCreateInfo renderpassInfo({}, 1, &colorAttachment, 1, &subpass, 1, &dependency);

        m_renderpass = m_context.getDevice().createRenderPass(renderpassInfo);

    }

    // base
    void createFramebuffers()
    {
        m_swapChainFramebuffers.resize(m_context.getSwapChainImageViews().size());
        for (size_t i = 0; i < m_context.getSwapChainImageViews().size(); i++)
        {
            vk::ImageView attachments[] = { m_context.getSwapChainImageViews().at(i) };

            vk::FramebufferCreateInfo framebufferInfo({}, m_renderpass,
                1, attachments,
                m_context.getSwapChainExtent().width,
                m_context.getSwapChainExtent().height,
                1);

            m_swapChainFramebuffers.at(i) = m_context.getDevice().createFramebuffer(framebufferInfo);
        }
    }

    // base
    void createCommandPools()
    {
        auto queueFamilyIndices = m_context.findQueueFamilies(m_context.getPhysicalDevice());

        // pool for graphics
        vk::CommandPoolCreateInfo graphicsPoolInfo({}, queueFamilyIndices.graphicsFamily.value());
        m_commandPool = m_context.getDevice().createCommandPool(graphicsPoolInfo);

        // pool for transfer
        vk::CommandPoolCreateInfo transferPoolInfo({}, queueFamilyIndices.transferFamily.value());
        m_transferCommandPool = m_context.getDevice().createCommandPool(transferPoolInfo);

        // pool for compute
        vk::CommandPoolCreateInfo computePoolInfo({}, queueFamilyIndices.computeFamily.value());
        m_computeCommandPool = m_context.getDevice().createCommandPool(transferPoolInfo);
    }

    void createCommandBuffers()
    {
        vk::CommandBufferAllocateInfo cmdAllocInfo(m_commandPool, vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(m_swapChainFramebuffers.size()));

        m_commandBuffers = m_context.getDevice().allocateCommandBuffers(cmdAllocInfo);

        for (size_t i = 0; i < m_commandBuffers.size(); i++)
        {
            vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eSimultaneousUse, nullptr);

            // begin recording
            m_commandBuffers.at(i).begin(beginInfo);

            vk::ClearValue clearColor(std::array<float, 4>{ 0.1f, 0.1f, 0.1f, 1.0f });
            vk::RenderPassBeginInfo renderpassInfo(m_renderpass, m_swapChainFramebuffers.at(i), { {0, 0}, m_context.getSwapChainExtent() }, 1, &clearColor);
            
            /////////////////////////////
            // actual commands start here
            /////////////////////////////
            m_commandBuffers.at(i).beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);

            m_commandBuffers.at(i).bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline);

            m_commandBuffers.at(i).bindVertexBuffers(0, m_vertexBufferInfo.m_Buffer, 0ull);

            m_commandBuffers.at(i).draw(static_cast<uint32_t>(vertices.size()), 1, 0, 0);

            m_commandBuffers.at(i).endRenderPass();

            // stop recording
            m_commandBuffers.at(i).end();
        }
    }

    // base
    void createSyncObjects()
    {
        m_imageAvailableSemaphores.resize(m_context.max_frames_in_flight);
        m_renderFinishedSemaphores.resize(m_context.max_frames_in_flight);
        m_inFlightFences.resize(m_context.max_frames_in_flight);

        vk::SemaphoreCreateInfo semaInfo;
        vk::FenceCreateInfo fenceInfo(vk::FenceCreateFlagBits::eSignaled);

        for(int i = 0; i < m_context.max_frames_in_flight; i++)
        {
            m_imageAvailableSemaphores.at(i) = m_context.getDevice().createSemaphore(semaInfo);
            m_renderFinishedSemaphores.at(i) = m_context.getDevice().createSemaphore(semaInfo);
            m_inFlightFences.at(i) = m_context.getDevice().createFence(fenceInfo);
        }

    }

    // base
    void drawFrame()
    {
        // wait for the last frame to be finished
        m_context.getDevice().waitForFences(m_inFlightFences.at(m_currentFrame), VK_TRUE, std::numeric_limits<uint64_t>::max());

        auto nextImageResult = m_context.getDevice().acquireNextImageKHR(m_context.getSwapChain(), std::numeric_limits<uint64_t>::max(), m_imageAvailableSemaphores.at(m_currentFrame), nullptr);
        uint32_t imageIndex = nextImageResult.value;

        // maybe change this to try/catch as shown below
        if(nextImageResult.result == vk::Result::eErrorOutOfDateKHR)
        {
            recreateSwapChain();
            return;
        }
        else if (nextImageResult.result != vk::Result::eSuccess && nextImageResult.result != vk::Result::eSuboptimalKHR)
        {
            throw std::runtime_error("Failed to acquire swap chain image");
        }

        vk::Semaphore waitSemaphores[] = { m_imageAvailableSemaphores.at(m_currentFrame) };
        vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
        vk::Semaphore signalSemaphores[] = { m_renderFinishedSemaphores.at(m_currentFrame) };

        vk::SubmitInfo submitInfo(1, waitSemaphores, waitStages, 1, &m_commandBuffers.at(imageIndex), 1, signalSemaphores);

        m_context.getDevice().resetFences(m_inFlightFences.at(m_currentFrame));
        m_context.getGraphicsQueue().submit(submitInfo, m_inFlightFences.at(m_currentFrame));

        std::array<vk::SwapchainKHR, 1> swapChains = { m_context.getSwapChain() };

        vk::PresentInfoKHR presentInfo(1, signalSemaphores, static_cast<uint32_t>(swapChains.size()), swapChains.data(), &imageIndex, nullptr);

        vk::Result presentResult = vk::Result::eSuccess;

        try
        {
            presentResult = m_context.getPresentQueue().presentKHR(presentInfo);
        }
        catch (const vk::OutOfDateKHRError&)
        {
            m_context.setFrameBufferResized(false);
            recreateSwapChain();
        }

        if (presentResult == vk::Result::eSuboptimalKHR || m_context.getFrameBufferResized())
        {
            m_context.setFrameBufferResized(false);
            recreateSwapChain();
        }

        //if (result != vk::Result::eSuccess)
        //    throw std::runtime_error("Failed to present");

        m_context.getPresentQueue().waitIdle();

        m_currentFrame = (m_currentFrame + 1) % m_context.max_frames_in_flight;
    }

    void mainLoop()
    {
        while (!glfwWindowShouldClose(m_context.getWindow()))
        {
            glfwPollEvents();
            drawFrame();
        }

        m_context.getDevice().waitIdle();
    }

private:
    vg::Context m_context;

    vk::RenderPass m_renderpass;

    vk::PipelineLayout m_pipelineLayout;
    vk::Pipeline m_graphicsPipeline;

    std::vector<vk::Framebuffer> m_swapChainFramebuffers;

    vk::CommandPool m_commandPool;
    vk::CommandPool m_transferCommandPool;
    vk::CommandPool m_computeCommandPool;

    std::vector<vk::CommandBuffer> m_commandBuffers;

    std::vector<vk::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::Fence> m_inFlightFences;
    int m_currentFrame = 0;

    BufferInfo m_vertexBufferInfo;


};


int main()
{
    App app;

    try
    {
        app.mainLoop();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        std::terminate();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}