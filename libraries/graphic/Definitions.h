#pragma once
#include <filesystem>
#include <optional>
#include <vulkan/vulkan.hpp>
#include "glm/glm.hpp"
#include <fstream>
#include <iostream>

namespace vg
{
    const std::vector<const char*> g_validationLayers = { "VK_LAYER_LUNARG_standard_validation", "VK_LAYER_LUNARG_assistant_layer" };
    const std::vector<const char*> g_deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_shader_draw_parameters" };

    const auto g_resourcesPath = std::filesystem::current_path().parent_path().parent_path().append("resources");
    const auto g_shaderPath = std::filesystem::current_path().parent_path().parent_path().append("shaders");

#ifdef NDEBUG
    constexpr bool enableValidationLayers = false;
#else
    constexpr bool enableValidationLayers = true;
#endif
    struct ImageLoadInfo
    {
        unsigned char* pixels;
        int texWidth, texHeight, texChannels;
        uint32_t mipLevels;
    };

    struct SemaphoreInfos
    {
        uint32_t waitSemaphoreCount = 0;
        vk::Semaphore* waitSemaphores = nullptr;
        uint32_t signalSemaphoreCount = 0;
        vk::Semaphore* signalSemaphores = nullptr;
        vk::PipelineStageFlags waitStageFlags = {};
    };

    struct QueueFamilyIndices
    {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        std::optional<uint32_t> transferFamily;
        std::optional<uint32_t> computeFamily;

        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value()
                && transferFamily.has_value() && computeFamily.has_value();
        }
    };

    enum class BufferBindings : uint32_t
    {
        VertexBuffer = 0
    };

    struct SwapChainSupportDetails
    {
        vk::SurfaceCapabilitiesKHR m_capabilities;
        std::vector<vk::SurfaceFormatKHR> m_formats;
        std::vector<vk::PresentModeKHR> m_presentModes;
    };

    struct VertexPosUvNormal
    {
        glm::vec3 pos;
        int32_t texID = -1;
        glm::vec2 uv;
        uint32_t pad2 = 0;
        uint32_t pad3 = 0;
        glm::vec3 normal;
        uint32_t pad4 = 0;

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription desc(0, sizeof(VertexPosUvNormal), vk::VertexInputRate::eVertex);
            return desc;
        }

        static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions;
            attributeDescriptions.at(0).binding = static_cast<uint32_t>(BufferBindings::VertexBuffer);
            attributeDescriptions.at(0).location = 0;
            attributeDescriptions.at(0).format = vk::Format::eR32G32B32Sfloat;
            attributeDescriptions.at(0).offset = offsetof(VertexPosUvNormal, pos);

            attributeDescriptions.at(1).binding = static_cast<uint32_t>(BufferBindings::VertexBuffer);;
            attributeDescriptions.at(1).location = 1;
            attributeDescriptions.at(1).format = vk::Format::eR32G32Sfloat;
            attributeDescriptions.at(1).offset = offsetof(VertexPosUvNormal, uv);

            attributeDescriptions.at(2).binding = static_cast<uint32_t>(BufferBindings::VertexBuffer);;
            attributeDescriptions.at(2).location = 2;
            attributeDescriptions.at(2).format = vk::Format::eR32G32B32Sfloat;
            attributeDescriptions.at(2).offset = offsetof(VertexPosUvNormal, normal);

            return attributeDescriptions;
        }
    };

    struct VertexPosUv
    {
        glm::vec3 pos;
        glm::vec2 uv;

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription desc(0, sizeof(VertexPosUv), vk::VertexInputRate::eVertex);
            return desc;
        }

        static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 2> attributeDescriptions;
            attributeDescriptions.at(0).binding = static_cast<uint32_t>(BufferBindings::VertexBuffer);
            attributeDescriptions.at(0).location = 0;
            attributeDescriptions.at(0).format = vk::Format::eR32G32B32Sfloat;
            attributeDescriptions.at(0).offset = offsetof(VertexPosUv, pos);

            attributeDescriptions.at(1).binding = static_cast<uint32_t>(BufferBindings::VertexBuffer);;
            attributeDescriptions.at(1).location = 1;
            attributeDescriptions.at(1).format = vk::Format::eR32G32Sfloat;
            attributeDescriptions.at(1).offset = offsetof(VertexPosUv, uv);

            return attributeDescriptions;
        }
    };

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

    class Utility
    {
    public:
        static std::vector<char> readFile(const std::filesystem::path& filePath)
        {
            std::ifstream file(vg::g_shaderPath / filePath, std::ios::ate | std::ios::binary);

            if (!file.is_open())
                throw std::runtime_error(std::string("Failed to open file") + filePath.filename().string());

            auto fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(fileSize);

            file.seekg(0);
            file.read(buffer.data(), fileSize);

            file.close();

            return buffer;
        }
    };

}


