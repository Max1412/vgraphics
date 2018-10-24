#pragma once

#include <filesystem>
#include "glm/glm.hpp"
#include <vulkan/vulkan.hpp>
#include "graphic/Definitions.h"


struct Mesh
{
    vk::DeviceSize numVertices = 0;
    vk::DeviceSize numIndices = 0;
    vk::DeviceSize vertexOffset = 0;
    vk::DeviceSize indexOffset = 0;
    vk::DeviceSize modelMatrixIndex = 0;
};


class Scene
{
public:
    explicit Scene(const std::filesystem::path& scenePath);

private:
    std::vector<uint32_t> m_allIndices;
    std::vector<vg::VertexPosUv> m_allVertices;
    std::vector<Mesh> m_meshes;
    std::vector<glm::mat4> m_modelMatrices;
};
