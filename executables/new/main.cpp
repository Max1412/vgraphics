#include <vulkan/vulkan.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // use Vulkans depth range [0, 1]
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

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
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;


    static vk::VertexInputBindingDescription getBindingDescription()
    {
        vk::VertexInputBindingDescription desc(0, sizeof(Vertex), vk::VertexInputRate::eVertex);
        return desc;
    }

    static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions()
    {
        std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions;
        attributeDescriptions.at(0).binding = 0;
        attributeDescriptions.at(0).location = 0;
        attributeDescriptions.at(0).format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions.at(0).offset = offsetof(Vertex, pos);

        attributeDescriptions.at(1).binding = 0;
        attributeDescriptions.at(1).location = 1;
        attributeDescriptions.at(1).format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions.at(1).offset = offsetof(Vertex, color);

        attributeDescriptions.at(2).binding = 0;
        attributeDescriptions.at(2).location = 2;
        attributeDescriptions.at(2).format = vk::Format::eR32G32Sfloat;
        attributeDescriptions.at(2).offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }
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

            return suitable && indices.isComplete() && extensionSupport && swapChainAdequate && features.samplerAnisotropy;
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
            deviceFeatures.samplerAnisotropy = VK_TRUE;

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
                vk::ImageViewCreateInfo createInfo({}, m_swapChainImages.at(i), vk::ImageViewType::e2D, m_swapChainImageFormat, {}, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });
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

        const int max_frames_in_flight = 2;

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
    inline static const auto s_resourcesPath = std::filesystem::current_path().parent_path().parent_path().append("resources");
private:
    inline static const auto s_shaderPath = std::filesystem::current_path().parent_path().parent_path().append("shaders");

};


class App
{
public:
    App()
    {
        createRenderPass();
        createDescriptorSetLayout();

        createGraphicsPipeline();
        createCommandPools();

        createDepthResources();
        createFramebuffers();

        createTextureImage();
        createTextureImageView();
        createTextureSampler();

        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();

        createCommandBuffers();
        createSyncObjects();
    }

    // todo clarify what is here and what is in cleanupswapchain
    ~App()
    {
        vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_indexBufferInfo.m_Buffer), m_indexBufferInfo.m_BufferAllocation);
        vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_vertexBufferInfo.m_Buffer), m_vertexBufferInfo.m_BufferAllocation);

        m_context.getDevice().destroyImageView(m_depthImageView);
        vmaDestroyImage(m_context.getAllocator(), m_depthImage.m_Image, m_depthImage.m_ImageAllocation);

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

        m_context.getDevice().destroySampler(m_textureSampler);
        m_context.getDevice().destroyImageView(m_textureImageView);
        vmaDestroyImage(m_context.getAllocator(), m_image.m_Image, m_image.m_ImageAllocation);

        m_context.getDevice().destroyDescriptorPool(m_descriptorPool);
        m_context.getDevice().destroyDescriptorSetLayout(m_descriptorSetLayout);

        for (size_t i = 0; i < m_swapChainFramebuffers.size(); i++)
        {
            vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(m_uniformBufferInfos.at(i).m_Buffer), m_uniformBufferInfos.at(i).m_BufferAllocation);
        }

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
    struct ImageInfo
    {
        vk::Image m_Image = nullptr;
        VmaAllocation m_ImageAllocation = nullptr;
        VmaAllocationInfo m_ImageAllocInfo = {};
    };

    struct UniformBufferObject
    {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };

    void createDescriptorSetLayout()
    {
        vk::DescriptorSetLayoutBinding uboLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr);

        vk::DescriptorSetLayoutBinding samplerLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr);

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {uboLayoutBinding, samplerLayoutBinding};

        vk::DescriptorSetLayoutCreateInfo layoutInfo({}, static_cast<uint32_t>(bindings.size()), bindings.data());

        m_descriptorSetLayout = m_context.getDevice().createDescriptorSetLayout(layoutInfo);

    }


    // base 
    BufferInfo createBuffer(const vk::DeviceSize size, const vk::BufferUsageFlags& usage, const VmaMemoryUsage properties, vk::SharingMode sharingMode = vk::SharingMode::eExclusive, VmaAllocationCreateFlags flags = 0)
    {
        BufferInfo bufferInfo;

        vk::BufferCreateInfo bufferCreateInfo({}, size, usage, sharingMode);
        if(sharingMode == vk::SharingMode::eConcurrent)
        {
            vg::Context::QueueFamilyIndices indices = m_context.findQueueFamilies(m_context.getPhysicalDevice());
            uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.transferFamily.value() };

            bufferCreateInfo.queueFamilyIndexCount = 2;
            bufferCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = properties;
        allocInfo.flags = flags;
        const auto result = vmaCreateBuffer(m_context.getAllocator(),
            reinterpret_cast<VkBufferCreateInfo*>(&bufferCreateInfo), &allocInfo, reinterpret_cast<VkBuffer*>(&bufferInfo.m_Buffer), &bufferInfo.m_BufferAllocation, &bufferInfo.m_BufferAllocInfo);

        if (result != VK_SUCCESS)
            throw std::runtime_error("Buffer creation failed");

        return bufferInfo;
    }

    // base
    void copyBuffer(const vk::Buffer src, const vk::Buffer dst, const vk::DeviceSize size) const
    {
        vk::CommandBufferAllocateInfo allocInfo(m_transferCommandPool, vk::CommandBufferLevel::ePrimary, 1);

        auto commandBuffer = beginSingleTimeCommands(m_transferCommandPool);

        vk::BufferCopy copyRegion(0, 0, size);
        commandBuffer.copyBuffer(src, dst, copyRegion);

        endSingleTimeCommands(commandBuffer, m_context.getTransferQueue(), m_transferCommandPool);
    }

    // base
    template <typename T>
    BufferInfo fillBufferTroughStagedTransfer(const std::vector<T>& data, const vk::BufferUsageFlags actualBufferUsage)
    {
        vk::DeviceSize bufferSize = sizeof(T) * data.size();

        auto stagingBufferInfo = createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY, vk::SharingMode::eConcurrent, VMA_ALLOCATION_CREATE_MAPPED_BIT);

        // staging buffer is persistently mapped, no mapping necessary
        memcpy(stagingBufferInfo.m_BufferAllocInfo.pMappedData, data.data(), stagingBufferInfo.m_BufferAllocInfo.size); // TODO maybe using this size is wrong

        auto returnBufferInfo = createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | actualBufferUsage, VMA_MEMORY_USAGE_GPU_ONLY, vk::SharingMode::eConcurrent);

        copyBuffer(stagingBufferInfo.m_Buffer, returnBufferInfo.m_Buffer, bufferSize);

        vmaDestroyBuffer(m_context.getAllocator(), static_cast<VkBuffer>(stagingBufferInfo.m_Buffer), stagingBufferInfo.m_BufferAllocation);

        return returnBufferInfo;
    }

    void createVertexBuffer()
    {
        m_vertexBufferInfo = fillBufferTroughStagedTransfer(m_vertices, vk::BufferUsageFlagBits::eVertexBuffer);
    }

    void createIndexBuffer()
    {
        m_indexBufferInfo = fillBufferTroughStagedTransfer(m_indices, vk::BufferUsageFlagBits::eIndexBuffer);
    }

    void createUniformBuffers()
    {
        for (size_t i = 0; i < m_context.getSwapChainImageViews().size(); i++)
        {
            auto buffer = createBuffer(sizeof(UniformBufferObject), vk::BufferUsageFlagBits::eUniformBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
            m_uniformBufferInfos.push_back(buffer);            
        }
    }

    void createTextureImage()
    {
        int texWidth, texHeight, texChannels;
        auto path = Utility::s_resourcesPath;
        path.append("texture.jpg");
        stbi_uc* pixels = stbi_load(path.string().c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        vk::DeviceSize imageSize = texWidth * texHeight * 4;

        if (!pixels)
            throw std::runtime_error("Failed to load image");

        auto stagingBuffer = createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY, vk::SharingMode::eExclusive, VMA_ALLOCATION_CREATE_MAPPED_BIT);
        memcpy(stagingBuffer.m_BufferAllocInfo.pMappedData, pixels, static_cast<size_t>(imageSize));

        stbi_image_free(pixels);

        using us = vk::ImageUsageFlagBits;
        m_image = createImage(texWidth, texHeight, vk::Format::eR8G8B8A8Unorm, vk::ImageTiling::eOptimal, us::eTransferDst | us::eSampled, VMA_MEMORY_USAGE_GPU_ONLY);

        transitionImageLayout(m_image.m_Image, vk::Format::eR8G8B8A8Unorm, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

        copyBufferToImage(stagingBuffer.m_Buffer, m_image.m_Image, texWidth, texHeight);

        transitionImageLayout(m_image.m_Image, vk::Format::eR8G8B8A8Unorm, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

        vmaDestroyBuffer(m_context.getAllocator(), stagingBuffer.m_Buffer, stagingBuffer.m_BufferAllocation);
    }

    // base/image class
    void transitionImageLayout(vk::Image image, vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout)
    {
        auto cmdBuffer = beginSingleTimeCommands(m_commandPool);

        using il = vk::ImageLayout;
        using af = vk::AccessFlagBits;
        using ps = vk::PipelineStageFlagBits;

        vk::ImageMemoryBarrier barrier;
        barrier.setOldLayout(oldLayout);
        barrier.setNewLayout(newLayout);
        barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setImage(image);
        vk::ImageSubresourceRange subresrange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        barrier.setSubresourceRange(subresrange);

        if (newLayout == il::eDepthStencilAttachmentOptimal)
        {
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;

            if (format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint || format == vk::Format::eD16UnormS8Uint)
            {
                barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
            }
        }

        vk::PipelineStageFlags sourceStage;
        vk::PipelineStageFlags destinationStage;

        if (oldLayout == il::eUndefined && newLayout == il::eTransferDstOptimal)
        {
            barrier.srcAccessMask = static_cast<af>(0);
            barrier.dstAccessMask = af::eTransferWrite;

            sourceStage = ps::eTopOfPipe;
            destinationStage = ps::eTransfer;
        }
        else if (oldLayout == il::eTransferDstOptimal && newLayout == il::eShaderReadOnlyOptimal)
        {
            barrier.srcAccessMask = af::eTransferWrite;
            barrier.dstAccessMask = af::eShaderRead;

            sourceStage = ps::eTransfer;
            destinationStage = ps::eFragmentShader;
        }
        else if (oldLayout == il::eUndefined && newLayout == il::eDepthStencilAttachmentOptimal)
        {
            barrier.srcAccessMask = static_cast<af>(0);
            barrier.dstAccessMask = af::eDepthStencilAttachmentRead | af::eDepthStencilAttachmentWrite;

            sourceStage = ps::eTopOfPipe;
            destinationStage = ps::eEarlyFragmentTests;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }

        cmdBuffer.pipelineBarrier(sourceStage, destinationStage, static_cast<vk::DependencyFlagBits>(0) , 0, nullptr, 0, nullptr, 1, &barrier);

        endSingleTimeCommands(cmdBuffer, m_context.getGraphicsQueue(), m_commandPool);
    }

    // base/image class
    // todo maybe make graphics/transfer queue selectable somehow. needs to assure the resources are conurrently shared
    void copyBufferToImage(vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height)
    {
        auto cmdBuffer = beginSingleTimeCommands(m_commandPool);

        vk::ImageSubresourceLayers subreslayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        vk::BufferImageCopy region(0, 0, 0, subreslayers, { 0, 0, 0 }, { width, height, 1 });

        cmdBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);

        endSingleTimeCommands(cmdBuffer, m_context.getGraphicsQueue(), m_commandPool);
    }

    // base, maybe cmd buffer abstraction?
    vk::CommandBuffer beginSingleTimeCommands(vk::CommandPool commandPool) const
    {
        vk::CommandBufferAllocateInfo allocInfo(commandPool, vk::CommandBufferLevel::ePrimary, 1);
        auto cmdBuffer = m_context.getDevice().allocateCommandBuffers(allocInfo).at(0);

        vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmdBuffer.begin(beginInfo);

        return cmdBuffer;
    }

    // base
    void endSingleTimeCommands(vk::CommandBuffer commandBuffer, vk::Queue queue, vk::CommandPool commandPool) const
    {
        commandBuffer.end();

        vk::SubmitInfo submitInfo({}, nullptr, nullptr, 1, &commandBuffer);

        queue.submit(submitInfo, nullptr);
        queue.waitIdle();

        m_context.getDevice().freeCommandBuffers(commandPool, commandBuffer);
    }

    // base
    ImageInfo createImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usageFlags,
        const VmaMemoryUsage properties, vk::SharingMode sharingMode = vk::SharingMode::eExclusive, VmaAllocationCreateFlags flags = 0)
    {
        ImageInfo returnInfo;

        vk::ImageCreateInfo createInfo({}, vk::ImageType::e2D, format, { width, height, 1 }, 1, 1, vk::SampleCountFlagBits::e1, tiling, usageFlags, sharingMode);
        if (sharingMode == vk::SharingMode::eConcurrent)
        {
            vg::Context::QueueFamilyIndices indices = m_context.findQueueFamilies(m_context.getPhysicalDevice());
            uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.transferFamily.value() };

            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = properties;
        allocInfo.flags = flags;
        vmaCreateImage(m_context.getAllocator(),
            reinterpret_cast<VkImageCreateInfo*>(&createInfo), &allocInfo,
            reinterpret_cast<VkImage*>(&returnInfo.m_Image), &returnInfo.m_ImageAllocation, &returnInfo.m_ImageAllocInfo);

        return returnInfo;
    }

    void createTextureImageView()
    {
        vk::ImageViewCreateInfo viewInfo({}, m_image.m_Image, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Unorm, {}, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });
        m_textureImageView = m_context.getDevice().createImageView(viewInfo);
    }

    void createTextureSampler()
    {
        vk::SamplerCreateInfo samplerInfo({},
            vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
            0.0f, VK_TRUE, 16.0f, VK_FALSE, vk::CompareOp::eAlways, 0.0f, 0.0f, vk::BorderColor::eIntOpaqueBlack, VK_FALSE
        );

        m_textureSampler = m_context.getDevice().createSampler(samplerInfo);
    }

    void createDepthResources()
    {
        // skipping "findSupportedFormat"
        vk::Format depthFormat = vk::Format::eD32SfloatS8Uint;
        m_depthImage = createImage(m_context.getSwapChainExtent().width, m_context.getSwapChainExtent().height,
            depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, VMA_MEMORY_USAGE_GPU_ONLY);

        vk::ImageViewCreateInfo viewInfo({}, m_depthImage.m_Image, vk::ImageViewType::e2D, depthFormat, {}, { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 });
        m_depthImageView = m_context.getDevice().createImageView(viewInfo);

        transitionImageLayout(m_depthImage.m_Image, depthFormat, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    }

    // todo change this when uniform buffer changes
    void createDescriptorPool()
    {
        // todo change descriptor count here to have as many of the type specified here as I want
        vk::DescriptorPoolSize poolSizeUniformBuffer(vk::DescriptorType::eUniformBuffer, static_cast<uint32_t>(m_context.getSwapChainImageViews().size()));
        vk::DescriptorPoolSize poolSizeCombinedImageSampler(vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(m_context.getSwapChainImageViews().size()));

        std::array<vk::DescriptorPoolSize, 2> poolSizes = { poolSizeUniformBuffer, poolSizeCombinedImageSampler };

        vk::DescriptorPoolCreateInfo poolInfo({}, static_cast<uint32_t>(m_context.getSwapChainImageViews().size()), static_cast<uint32_t>(poolSizes.size()), poolSizes.data());

        m_descriptorPool = m_context.getDevice().createDescriptorPool(poolInfo);
    }

    void createDescriptorSets()
    {
        std::vector<vk::DescriptorSetLayout> layouts(m_swapChainFramebuffers.size(), m_descriptorSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo(m_descriptorPool, static_cast<uint32_t>(m_swapChainFramebuffers.size()), layouts.data());

        m_descriptorSets = m_context.getDevice().allocateDescriptorSets(allocInfo);

        for (int i = 0; i < m_swapChainFramebuffers.size(); i++)
        {
            vk::DescriptorBufferInfo bufferInfo(m_uniformBufferInfos.at(i).m_Buffer, 0, sizeof(UniformBufferObject));
            vk::WriteDescriptorSet descWrite(m_descriptorSets.at(i), 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufferInfo, nullptr);

            vk::DescriptorImageInfo imageInfo(m_textureSampler, m_textureImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
            vk::WriteDescriptorSet descWriteImage(m_descriptorSets.at(i), 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo, nullptr, nullptr);

            std::array<vk::WriteDescriptorSet, 2> descriptorWrites = {descWrite, descWriteImage};

            m_context.getDevice().updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        }
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
        createDepthResources();
        createFramebuffers();
        createCommandBuffers();
    }

    // base
    void cleanUpSwapchain()
    {
        m_context.getDevice().destroyImageView(m_depthImageView);
        vmaDestroyImage(m_context.getAllocator(), m_depthImage.m_Image, m_depthImage.m_ImageAllocation);


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

        vk::PipelineRasterizationStateCreateInfo rasterizer({}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise, VK_FALSE, 0, 0, 0, 1.0f);

        vk::PipelineMultisampleStateCreateInfo mulitsampling({}, vk::SampleCountFlagBits::e1, VK_FALSE);

        vk::PipelineDepthStencilStateCreateInfo depthStencil({}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess);

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
        pipelineLayoutInfo.setSetLayoutCount(1);
        pipelineLayoutInfo.setPushConstantRangeCount(0);
        pipelineLayoutInfo.setPSetLayouts(&m_descriptorSetLayout);

        m_pipelineLayout = m_context.getDevice().createPipelineLayout(pipelineLayoutInfo);
        
        vk::GraphicsPipelineCreateInfo pipelineInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &mulitsampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
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
        
        vk::AttachmentDescription depthAttachment({}, vk::Format::eD32SfloatS8Uint,
            vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare,
            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal
        );

        vk::AttachmentReference depthAttachmentRef(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

        vk::SubpassDependency dependency(VK_SUBPASS_EXTERNAL, 0,
            vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
            {}, vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite, vk::DependencyFlagBits::eByRegion);


        vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics,
            0, nullptr,                 // input attachments (standard values)
            1, &colorAttachmentRef,     // color attachments: layout (location = 0) out -> colorAttachmentRef is at index 0
            nullptr,                    // no resolve attachment
            &depthAttachmentRef);       // depth stencil attachment
                                        // other attachment at standard values: Preserved

        std::array<vk::AttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

        vk::RenderPassCreateInfo renderpassInfo({}, static_cast<uint32_t>(attachments.size()), attachments.data(), 1, &subpass, 1, &dependency);

        m_renderpass = m_context.getDevice().createRenderPass(renderpassInfo);

    }

    // base
    void createFramebuffers()
    {
        m_swapChainFramebuffers.resize(m_context.getSwapChainImageViews().size());
        for (size_t i = 0; i < m_context.getSwapChainImageViews().size(); i++)
        {
            std::array<vk::ImageView, 2> attachments = { m_context.getSwapChainImageViews().at(i), m_depthImageView };

            vk::FramebufferCreateInfo framebufferInfo({}, m_renderpass,
                static_cast<uint32_t>(attachments.size()), attachments.data(),
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

            std::array<vk::ClearValue, 2> clearColors = { vk::ClearValue{std::array<float, 4>{ 0.1f, 0.1f, 0.1f, 1.0f }}, vk::ClearDepthStencilValue{1.0f, 0} };
            vk::RenderPassBeginInfo renderpassInfo(m_renderpass, m_swapChainFramebuffers.at(i), { {0, 0}, m_context.getSwapChainExtent() }, static_cast<uint32_t>(clearColors.size()), clearColors.data());
            
            /////////////////////////////
            // actual commands start here
            /////////////////////////////
            m_commandBuffers.at(i).beginRenderPass(renderpassInfo, vk::SubpassContents::eInline);

            m_commandBuffers.at(i).bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline);

            m_commandBuffers.at(i).bindVertexBuffers(0, m_vertexBufferInfo.m_Buffer, 0ull);
            m_commandBuffers.at(i).bindIndexBuffer(m_indexBufferInfo.m_Buffer, 0ull, vk::IndexType::eUint16);

            m_commandBuffers.at(i).bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout, 0, 1, &m_descriptorSets.at(i), 0, nullptr);

            m_commandBuffers.at(i).drawIndexed(static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);

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

        updateUniformBuffer(imageIndex);

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

    // TODO this is bs, doesnt need to be triple buffered, can use vkCmdUpdateBuffer
    // TODO and push constants are better for view matrix I guess
    void updateUniformBuffer(uint32_t currentImage)
    {
        // time needed for rotation
        // todo use camera instead
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        UniformBufferObject ubo = {};
        ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), m_context.getSwapChainExtent().width / static_cast<float>(m_context.getSwapChainExtent().height), 0.1f, 10.0f);
        
        // OpenGL space to Vulkan Space
        ubo.proj[1][1] *= -1;

        void* mappedData;
        vmaMapMemory(m_context.getAllocator(), m_uniformBufferInfos.at(currentImage).m_BufferAllocation, &mappedData);
        memcpy(mappedData, &ubo, sizeof(ubo));
        vmaUnmapMemory(m_context.getAllocator(), m_uniformBufferInfos.at(currentImage).m_BufferAllocation);


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

    vk::DescriptorSetLayout m_descriptorSetLayout;

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
    BufferInfo m_indexBufferInfo;
    std::vector<BufferInfo> m_uniformBufferInfos;

    vk::DescriptorPool m_descriptorPool;
    std::vector<vk::DescriptorSet> m_descriptorSets;

    ImageInfo m_image;
    vk::ImageView m_textureImageView;
    vk::Sampler m_textureSampler;

    ImageInfo m_depthImage;
    vk::ImageView m_depthImageView;

    // todo uniform buffer:
    // 1 big for per-mesh attributes (model matrix, later material, ...), doesnt change
    // push constants for view matrix
    // uniform buffer for projection matrix, changes only on window resize
    // important patterns:
    //      pack data together that changes together
    //      SoA over AoS

    // todo structure:
    // re-usable functions and members into baseapp, each app inherits from baseapp
    // make image/buffer creation/copy/... functions member functions of image/buffer/... classes (maybe)

    const std::vector<Vertex> m_vertices = {
        {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},

        {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}
    };

    const std::vector<uint16_t> m_indices = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4
    };

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