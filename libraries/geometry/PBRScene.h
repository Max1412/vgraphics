#pragma once

#include <filesystem>
#include "glm/glm.hpp"
#include <vulkan/vulkan.hpp>
#include "graphic/Definitions.h"
#include <set>
#include "graphic/BaseApp.h"


//struct Mesh
//{
//    vk::DeviceSize numVertices = 0;
//    vk::DeviceSize numIndices = 0;
//    vk::DeviceSize vertexOffset = 0;
//    vk::DeviceSize indexOffset = 0;
//    vk::DeviceSize modelMatrixIndex = 0;
//};

struct PerMeshInfoPBR : vk::DrawIndexedIndirectCommand
{
	int texIndexBaseColor = -1;
	int texIndexMetallicRoughness = -1;
    int assimpMaterialIndex = -1;
};

struct MaterialInfoPBR
{
	glm::vec3 baseColor;
	float roughness = -1.0f;
	glm::vec3 f0;
	float metalness = -1.0f;
};

class PBRScene
{
public:
    explicit PBRScene(const std::filesystem::path& filename);

    std::vector<vg::VertexPosUvNormal>& getVertices() { return m_allVertices; }
    const std::vector<uint32_t>& getIndices() const { return m_allIndices; }
    const std::vector<PerMeshInfoPBR>& getDrawCommandData() const { return m_meshes; }
    const std::vector<glm::mat4>& getModelMatrices() const { return m_modelMatrices; }
    const std::vector<std::pair<std::vector<unsigned>, std::string>>& getIndexedBaseColorTexturePaths() const { return m_indexedBaseColorTexturePaths;  }
	const std::vector<std::pair<std::vector<unsigned>, std::string>>& getIndexedMetallicRoughnessTexturePaths() const { return m_indexedMetallicRoughnessTexturePaths; }

    const std::vector<MaterialInfoPBR>& getMaterials() const { return m_allMaterials; }

private:
    std::vector<uint32_t> m_allIndices;
    std::vector<vg::VertexPosUvNormal> m_allVertices;
    std::vector<PerMeshInfoPBR> m_meshes;
    std::vector<glm::mat4> m_modelMatrices;

    std::set<std::string> m_texturesBaseColorPathSet;
	std::set<std::string> m_texturesMetallicRoughnessPathSet;

    std::vector<std::pair<std::vector<unsigned>, std::string>> m_indexedBaseColorTexturePaths;
	std::vector<std::pair<std::vector<unsigned>, std::string>> m_indexedMetallicRoughnessTexturePaths;

    std::vector<MaterialInfoPBR> m_allMaterials;
};
