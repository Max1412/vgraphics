#include <vulkan/vulkan.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // use Vulkans depth range [0, 1]
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            m_window = glfwCreateWindow(m_width, m_height, "Vulkan", nullptr, nullptr);
        }

        void initVulkan()
        {
            createInstance();
            setupDebugCallback();

            createSurface();

            pickPhysicalDevice();
            createLogicalDevice();

            createSwapChain();
            createImageViews();
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

            vk::InstanceCreateInfo createInfo({}, &appInfo, layerNames.size(), layerNames.data(), requiredExtensions.size(), requiredExtensions.data());


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
                sevFlags::eError | sevFlags::eWarning | sevFlags::eVerbose, // | sevFlags::eInfo,
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

            bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
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

            int i = 0;
            for (const auto& queueFamily : qfprops)
            {
                if (queueFamily.queueCount > 0 && queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
                    indices.graphicsFamily = i;

                bool presentSupport = physDevice.getSurfaceSupportKHR(i, m_surface);
                if (queueFamily.queueCount > 0 && presentSupport)
                    indices.presentFamily = i;

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
            std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

            float queuePriority = 1.0f;
            for (uint32_t queueFamily : uniqueQueueFamilies)
            {
                vk::DeviceQueueCreateInfo queueCreateInfo({}, queueFamily, 1, &queuePriority);
                queueCreateInfos.push_back(queueCreateInfo);
            }

            vk::PhysicalDeviceFeatures deviceFeatures;

            vk::DeviceCreateInfo createInfo({}, queueCreateInfos.size(), queueCreateInfos.data(), 0, nullptr, deviceExtensions.size(), deviceExtensions.data(), &deviceFeatures);

            if constexpr(enableValidationLayers)
            {
                createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
                createInfo.ppEnabledLayerNames = validationLayers.data();
            }

            m_device = m_phsyicalDevice.createDevice(createInfo);

            m_presentQueue = m_device.getQueue(indices.presentFamily.value(), 0);
            m_graphicsQueue = m_device.getQueue(indices.graphicsFamily.value(), 0);
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
                VkExtent2D actualExtent = { m_width, m_height };
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

        VmaAllocator getAllocator() const { return m_allocator; }

    private:
        // vk initialisation objects
        vk::Instance m_instance;
        vk::DebugUtilsMessengerEXT m_callback;
        vk::PhysicalDevice m_phsyicalDevice;
        vk::Device m_device;
        vk::Queue m_graphicsQueue;

        // presentation objects
        vk::SurfaceKHR m_surface;
        vk::Queue m_presentQueue;
        vk::SwapchainKHR m_swapchain;
        vk::Format m_swapChainImageFormat;
        vk::Extent2D m_swapChainExtent;
        std::vector<vk::Image> m_swapChainImages;
        std::vector<vk::ImageView> m_swapChainImageViews;

        VmaAllocator m_allocator;

        GLFWwindow*  m_window;
        const int m_width = 1600;
        const int m_height = 900;
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
        createCommandPool();
        createCommandBuffers();
        createSemaphores();
    }

    ~App()
    {
        m_context.getDevice().destroySemaphore(m_imageAvailableSemaphore);
        m_context.getDevice().destroySemaphore(m_renderFinishedSemaphore);

        m_context.getDevice().destroyCommandPool(m_commandPool);
        for (const auto framebuffer : m_swapChainFramebuffers)
            m_context.getDevice().destroyFramebuffer(framebuffer);

        m_context.getDevice().destroyPipeline(m_graphicsPipeline);
        m_context.getDevice().destroyPipelineLayout(m_pipelineLayout);
        m_context.getDevice().destroyRenderPass(m_renderpass);
        // cleanup here
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

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo({}, 0, nullptr, 0, nullptr);
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

    void createCommandPool()
    {
        auto queueFamilyIndices = m_context.findQueueFamilies(m_context.getPhysicalDevice());

        vk::CommandPoolCreateInfo poolInfo({}, queueFamilyIndices.graphicsFamily.value());

        m_commandPool = m_context.getDevice().createCommandPool(poolInfo);
    }

    void createCommandBuffers()
    {
        vk::CommandBufferAllocateInfo cmdAllocInfo(m_commandPool, vk::CommandBufferLevel::ePrimary, m_swapChainFramebuffers.size());

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

            m_commandBuffers.at(i).draw(3, 1, 0, 0);

            m_commandBuffers.at(i).endRenderPass();

            // stop recording
            m_commandBuffers.at(i).end();
        }
    }

    void createSemaphores()
    {
        vk::SemaphoreCreateInfo semaInfo;
        m_imageAvailableSemaphore = m_context.getDevice().createSemaphore(semaInfo);
        m_renderFinishedSemaphore = m_context.getDevice().createSemaphore(semaInfo);

    }

    void drawFrame()
    {
        uint32_t imageIndex = m_context.getDevice().acquireNextImageKHR(m_context.getSwapChain(), std::numeric_limits<uint64_t>::max(), m_imageAvailableSemaphore, nullptr).value;

        vk::Semaphore waitSemaphores[] = { m_imageAvailableSemaphore };
        vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
        vk::Semaphore signalSemaphores[] = { m_renderFinishedSemaphore };

        vk::SubmitInfo submitInfo(1, waitSemaphores, waitStages, 1, &m_commandBuffers.at(imageIndex), 1, signalSemaphores);

        m_context.getGraphicsQueue().submit(submitInfo, nullptr);

        std::array<vk::SwapchainKHR, 1> swapChains = { m_context.getSwapChain() };
        vk::Result result;
        vk::PresentInfoKHR presentInfo(1, signalSemaphores, swapChains.size(), swapChains.data(), &imageIndex, &result);


        if(vk::Result::eSuccess != m_context.getPresentQueue().presentKHR(presentInfo))
            throw std::runtime_error("Failed to present");

        if (result != vk::Result::eSuccess)
            throw std::runtime_error("Failed to present");
    }

    void mainLoop()
    {
        while (!glfwWindowShouldClose(m_context.getWindow()))
        {
            glfwPollEvents();
            drawFrame();
        }
    }

private:
    vg::Context m_context;

    vk::RenderPass m_renderpass;

    vk::PipelineLayout m_pipelineLayout;
    vk::Pipeline m_graphicsPipeline;

    std::vector<vk::Framebuffer> m_swapChainFramebuffers;

    vk::CommandPool m_commandPool;
    std::vector<vk::CommandBuffer> m_commandBuffers;

    vk::Semaphore m_imageAvailableSemaphore;
    vk::Semaphore m_renderFinishedSemaphore;

};


int main() {
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