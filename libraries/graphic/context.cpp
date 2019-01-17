#include <set>
#define GLFW_INCLUDE_VULKAN
#include "Context.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_impl_glfw.h"
#include "spdlog/sinks/stdout_color_sinks.h"

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

//todo remove this when the SDK update happened
VkResult vkCreateAccelerationStructureNV(VkDevice device, const VkAccelerationStructureCreateInfoNV* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkAccelerationStructureNV* pAccelerationStructure) {
    auto func = reinterpret_cast<PFN_vkCreateAccelerationStructureNV>(vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureNV"));
    if (func != nullptr) {
        return func(device, pCreateInfo, pAllocator, pAccelerationStructure);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void vkDestroyAccelerationStructureNV(VkDevice device, VkAccelerationStructureNV accelerationStructure, const VkAllocationCallbacks* pAllocator) {
    auto func = reinterpret_cast<PFN_vkDestroyAccelerationStructureNV>(vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureNV"));
    if (func != nullptr) {
        return func(device, accelerationStructure, pAllocator);
    }
}

void vkGetAccelerationStructureMemoryRequirementsNV(VkDevice device, const VkAccelerationStructureMemoryRequirementsInfoNV* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) {
    auto func = reinterpret_cast<PFN_vkGetAccelerationStructureMemoryRequirementsNV>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureMemoryRequirementsNV"));
    if (func != nullptr) {
        return func(device, pInfo, pMemoryRequirements);
    }
}

VkResult vkBindAccelerationStructureMemoryNV(VkDevice device, uint32_t bindInfoCount, const VkBindAccelerationStructureMemoryInfoNV* pBindInfos) {
    auto func = reinterpret_cast<PFN_vkBindAccelerationStructureMemoryNV>(vkGetDeviceProcAddr(device, "vkBindAccelerationStructureMemoryNV"));
    if (func != nullptr) {
        return func(device, bindInfoCount, pBindInfos);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

VkResult vkGetAccelerationStructureHandleNV(
    VkDevice                                    device,
    VkAccelerationStructureNV                   accelerationStructure,
    size_t                                      dataSize,
    void*                                       pData)
{
    auto func = reinterpret_cast<PFN_vkGetAccelerationStructureHandleNV>(vkGetDeviceProcAddr(device, "vkGetAccelerationStructureHandleNV"));
    if (func != nullptr) {
        return func(device, accelerationStructure, dataSize, pData);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

VkResult vkCreateRayTracingPipelinesNV(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoNV* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
    auto func = reinterpret_cast<PFN_vkCreateRayTracingPipelinesNV>(vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesNV"));
    if (func != nullptr) {
        return func(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

VkResult vkGetRayTracingShaderGroupHandlesNV(VkDevice device, VkPipeline pipeline, uint32_t firstGroup, uint32_t groupCount, size_t dataSize, void* pData)
{
    auto func = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesNV>(vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesNV"));
    if (func != nullptr) {
        return func(device, pipeline, firstGroup, groupCount, dataSize, pData);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}


namespace vg
{

	Context::Context(const std::vector<const char*>& requiredDeviceExtensions) : m_requiredDeviceExtensions(requiredDeviceExtensions)
    {
		// init logger
		m_logger = spdlog::stdout_color_mt("standard");
		m_logger->info("Logger initialized.");

		// init rest
        initWindow();
        initVulkan();
        initImgui();
    }

    Context::~Context()
    {
        cleanupImgui();


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

    void Context::initWindow()
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        //glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        m_window = glfwCreateWindow(m_width, m_height, "Vulkan", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
    }

    void Context::framebufferResizeCallback(GLFWwindow* window, int width, int height)
    {
        auto context = reinterpret_cast<Context*>(glfwGetWindowUserPointer(window));
        context->m_frameBufferResized = true;
    }

    void Context::initVulkan()
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

    void Context::createAllocator()
    {
        VmaAllocatorCreateInfo createInfo = {};
        createInfo.device = m_device;
        createInfo.physicalDevice = m_phsyicalDevice;
        //createInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
        auto result = vmaCreateAllocator(&createInfo, &m_allocator);

        if (result != VK_SUCCESS)
            throw std::runtime_error("Failed to create Allocator");
    }

    void Context::createInstance()
    {
        if (enableValidationLayers && !checkValidationLayerSupport())
        {
            throw std::runtime_error("Validation layers requested, but not available!");
        }

        vk::ApplicationInfo appInfo("Vulkan Test New", 1, "No Engine", 1, VK_API_VERSION_1_1);

        // glfw + (cond.) debug layer
        auto requiredExtensions = getRequiredExtensions();

        std::vector<const char*> layerNames;

        if constexpr (enableValidationLayers)
            layerNames = g_validationLayers;

        vk::InstanceCreateInfo createInfo({}, &appInfo, static_cast<uint32_t>(layerNames.size()), layerNames.data(), static_cast<uint32_t>(requiredExtensions.size()), requiredExtensions.data());

        if (vk::createInstance(&createInfo, nullptr, &m_instance) != vk::Result::eSuccess)
            throw std::runtime_error("Instance could not be created");

        // print instance extensions
        // getAllSupportedExtensions(true);
    }

    std::vector<const char*> Context::getRequiredExtensions()
    {
        uint32_t glfwExtensionCount = 0;
        auto exts = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> extensions(exts, exts + glfwExtensionCount);

        if constexpr (enableValidationLayers)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        return extensions;
    }

    std::vector<vk::ExtensionProperties> Context::getAllSupportedExtensions()
    {
        return vk::enumerateInstanceExtensionProperties();
    }

    bool Context::checkValidationLayerSupport() const
    {
        auto availableLayers = vk::enumerateInstanceLayerProperties();
        
        for (const char* layerName : g_validationLayers)
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

    VKAPI_ATTR VkBool32 VKAPI_CALL Context::debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData)
    {
		auto logger = spdlog::get("standard");

        // todo log this properly depending on severity & type
		switch (messageSeverity)
		{
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
				logger->info("Validation layer: {} -- {}", vk::to_string(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageType)), pCallbackData->pMessage);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
				logger->info("Validation layer: {} -- {}", vk::to_string(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageType)), pCallbackData->pMessage);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
				logger->warn("Validation layer: {} -- {}", vk::to_string(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageType)), pCallbackData->pMessage);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: 
				logger->critical("Validation layer: {} -- {}", vk::to_string(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageType)), pCallbackData->pMessage);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT: break;
			default: break ;
		}
        return VK_FALSE;
    }

    void Context::setupDebugCallback()
    {
        if constexpr (!enableValidationLayers) return;

        using sevFlags = vk::DebugUtilsMessageSeverityFlagBitsEXT;
        using typeFlags = vk::DebugUtilsMessageTypeFlagBitsEXT;

        vk::DebugUtilsMessengerCreateInfoEXT createInfo(
            {},
            sevFlags::eError | sevFlags::eWarning | sevFlags::eVerbose,// | sevFlags::eInfo,
            typeFlags::eGeneral | typeFlags::ePerformance | typeFlags::eValidation,
            reinterpret_cast<PFN_vkDebugUtilsMessengerCallbackEXT>(debugCallback)
        );

        if (help::CreateDebugUtilsMessengerEXT(m_instance,
            reinterpret_cast<VkDebugUtilsMessengerCreateInfoEXT*>(&createInfo), nullptr,
            reinterpret_cast<VkDebugUtilsMessengerEXT *>(&m_callback)) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to set up debug callback!");
        }
    }

    void Context::pickPhysicalDevice()
    {
        // TODO scoring system preferring discrete GPUs
        auto physDevices = m_instance.enumeratePhysicalDevices();

        if (physDevices.empty())
            throw std::runtime_error("No physical devices found");

        for (const auto& device : physDevices)
        {
            if (isDeviceSuitable(device))
            {
                m_phsyicalDevice = device;
                break;
            }
        }

        if (!m_phsyicalDevice)
            throw std::runtime_error("No suitable physical device found");
    }

    bool Context::isDeviceSuitable(vk::PhysicalDevice physDevice)
    {
        // TODO use actual stuff here
        auto properties = physDevice.getProperties();
        auto features = physDevice.getFeatures();

		vk::PhysicalDeviceProperties2 props;
		if(std::find_if(m_requiredDeviceExtensions.begin(), m_requiredDeviceExtensions.end(),
			[](const char* input){ return (strcmp(input, "VK_NV_ray_tracing") == 0); })
			!= m_requiredDeviceExtensions.end())
		{
			m_raytracingProperties.emplace();
			props.pNext = &m_raytracingProperties.value();
		}
		physDevice.getProperties2(&props); //NVIDIA only?
		
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

    bool Context::checkDeviceExtensionSupport(vk::PhysicalDevice physDevice)
    {
        auto availableExtensions = physDevice.enumerateDeviceExtensionProperties();

        std::set<std::string> requiredExtensions(m_requiredDeviceExtensions.begin(), m_requiredDeviceExtensions.end());

        for (const auto& extension : availableExtensions)
            requiredExtensions.erase(extension.extensionName);

        return requiredExtensions.empty();
    }

    QueueFamilyIndices Context::findQueueFamilies(vk::PhysicalDevice physDevice) const
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

    void Context::createLogicalDevice()
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
        deviceFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;
        deviceFeatures.fragmentStoresAndAtomics = VK_TRUE;
        deviceFeatures.multiDrawIndirect = VK_TRUE;
        deviceFeatures.shaderStorageImageExtendedFormats = VK_TRUE;

        vk::DeviceCreateInfo createInfo({},
            static_cast<uint32_t>(queueCreateInfos.size()), queueCreateInfos.data(),
            0, nullptr,
            static_cast<uint32_t>(m_requiredDeviceExtensions.size()), m_requiredDeviceExtensions.data(), &deviceFeatures);

        if constexpr (enableValidationLayers)
        {
            createInfo.enabledLayerCount = static_cast<uint32_t>(g_validationLayers.size());
            createInfo.ppEnabledLayerNames = g_validationLayers.data();
        }

        m_device = m_phsyicalDevice.createDevice(createInfo);

        // get all the queues!
        m_presentQueue = m_device.getQueue(indices.presentFamily.value(), 0);
        m_graphicsQueue = m_device.getQueue(indices.graphicsFamily.value(), 0);
        m_transferQueue = m_device.getQueue(indices.transferFamily.value(), 0);
        m_computeQueue = m_device.getQueue(indices.computeFamily.value(), 0);
    }

    void Context::createSurface()
    {
        if (glfwCreateWindowSurface(m_instance, m_window, nullptr, reinterpret_cast<VkSurfaceKHR*>(&m_surface)) != VK_SUCCESS)
            throw std::runtime_error("Surface creation failed");
    }

    SwapChainSupportDetails Context::querySwapChainSupport(vk::PhysicalDevice physDevice) const
    {
        SwapChainSupportDetails details;
        details.m_capabilities = physDevice.getSurfaceCapabilitiesKHR(m_surface);
        details.m_formats = physDevice.getSurfaceFormatsKHR(m_surface);
        details.m_presentModes = physDevice.getSurfacePresentModesKHR(m_surface);

        return details;
    }

    vk::SurfaceFormatKHR Context::chooseSwapChainSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
    {
        if (availableFormats.size() == 1 && availableFormats.at(0).format == vk::Format::eUndefined)
            return { vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear };

        for (const auto& availableFormat : availableFormats)
        {
            if (availableFormat.format == vk::Format::eB8G8R8A8Unorm && availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
                return availableFormat;
        }

        return availableFormats.at(0);
    }

    vk::PresentModeKHR Context::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes)
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

    vk::Extent2D Context::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities)
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

    void Context::createSwapChain()
    {
        auto swapChainSupport = querySwapChainSupport(m_phsyicalDevice);

        auto surfaceFormat = chooseSwapChainSurfaceFormat(swapChainSupport.m_formats);
        auto presentMode = chooseSwapPresentMode(swapChainSupport.m_presentModes);
        auto extent = chooseSwapExtent(swapChainSupport.m_capabilities);

        // determine the number of images in the swapchain (queue length)
        uint32_t imageCount = swapChainSupport.m_capabilities.minImageCount + 1;
        if (swapChainSupport.m_capabilities.maxImageCount > 0 && imageCount > swapChainSupport.m_capabilities.maxImageCount)
            imageCount = swapChainSupport.m_capabilities.maxImageCount;

        vk::SwapchainCreateInfoKHR createInfo({}, m_surface, imageCount, surfaceFormat.format, surfaceFormat.colorSpace, extent, 1, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage);

        QueueFamilyIndices indices = findQueueFamilies(m_phsyicalDevice);
        uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

        if (indices.graphicsFamily != indices.presentFamily)
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

    void Context::createImageViews()
    {
        m_swapChainImageViews.resize(m_swapChainImages.size());
        for (size_t i = 0; i < m_swapChainImages.size(); i++)
        {
            vk::ImageViewCreateInfo createInfo({}, m_swapChainImages.at(i), vk::ImageViewType::e2D, m_swapChainImageFormat, {}, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });
            m_swapChainImageViews.at(i) = m_device.createImageView(createInfo);
        }
    }

    vk::ShaderModule Context::createShaderModule(const std::vector<char>& code) const
    {
        vk::ShaderModuleCreateInfo createInfo({}, code.size(), reinterpret_cast<const uint32_t*>(code.data()));
        return m_device.createShaderModule(createInfo);
    }

    void Context::initImgui()
    {
        ImGui::CreateContext();
        ImGui_ImplGlfw_InitForVulkan(m_window, true);

        // create imgui descriptor pool
        vk::DescriptorPoolSize poolSizeCombinedImageSampler(vk::DescriptorType::eCombinedImageSampler, 1);
        std::array<vk::DescriptorPoolSize, 1> poolSizes = { poolSizeCombinedImageSampler };
        vk::DescriptorPoolCreateInfo poolInfo({}, 1, static_cast<uint32_t>(poolSizes.size()), poolSizes.data());
        m_imguiDescriptorPool = m_device.createDescriptorPool(poolInfo);

        // create imgui renderpass
        vk::AttachmentDescription colorAttachment({}, m_swapChainImageFormat,
            vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,            // load store op
            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,      // stencil op
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR
        );
        vk::AttachmentReference colorAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal);

        vk::AttachmentDescription depthAttachment({}, vk::Format::eD32SfloatS8Uint,
            vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eDontCare,
            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal
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
        m_imguiRenderpass = m_device.createRenderPass(renderpassInfo);

        // init imgui
        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.Instance = static_cast<VkInstance>(m_instance);
        initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(m_phsyicalDevice);
        initInfo.Device = static_cast<VkDevice>(m_device);
        initInfo.QueueFamily = findQueueFamilies(m_phsyicalDevice).graphicsFamily.value();
        initInfo.Queue = m_graphicsQueue;
        initInfo.PipelineCache = nullptr;
        initInfo.DescriptorPool = m_imguiDescriptorPool;
        ImGui_ImplVulkan_Init(&initInfo, static_cast<VkRenderPass>(m_imguiRenderpass));
    }

    void Context::cleanupImgui()
    {
        ImGui_ImplVulkan_InvalidateFontUploadObjects();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        m_device.destroyDescriptorPool(m_imguiDescriptorPool);
        m_device.destroyRenderPass(m_imguiRenderpass);
    }

}