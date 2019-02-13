#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <algorithm>

namespace vg {
    struct BufferInfo;
}


struct DirectionalLight
{
    glm::vec3 direction;
    int32_t numShadowSamples = 1;
    glm::vec3 intensity;
    int32_t pad1 = 0;

};
using PBRDirectionalLight = DirectionalLight;

struct PointLight
{
    glm::vec3 position;
    float constant;
    glm::vec3 intensity;
    float linear;
    float quadratic;
    float radius;
    int32_t numShadowSamples = 1;
    int32_t pad0 = 0;

};

struct PBRPointLight
{
	glm::vec3 position;
	float radius;
	glm::vec3 intensity;
	int32_t numShadowSamples = 1;

};

struct SpotLight
{
    glm::vec3 position;
    float constant;
    glm::vec3 intensity;
    float linear;
    glm::vec3 direction;
    float quadratic;
    float cutoff;
    float outerCutoff;
    float radius;
    int32_t numShadowSamples = 1;
};

struct PBRSpotLight
{
	glm::vec3 position;
	float radius;
	glm::vec3 intensity;
	float cutoff;
	glm::vec3 direction;
	int32_t numShadowSamples = 1;
	float outerCutoff;
	int32_t pad0 = 0, pad1 = 0, pad2 = 0;
};

class LightManager
{
public:
    LightManager() = default;
    LightManager(std::vector<DirectionalLight> dirLights, std::vector<PointLight> pointLights, std::vector<SpotLight> spotLights);
    void lightGUI(const vg::BufferInfo& dirLightBuffer, const vg::BufferInfo& pointLightBuffer,
                  const vg::BufferInfo& spotLightBuffer, const bool showRT = false) const;
    const std::vector<DirectionalLight>& getDirectionalLights() const { return m_directionalLights; }
    const std::vector<PointLight>& getPointLights() const { return m_pointLights; }
    const std::vector<SpotLight>& getSpotLights() const { return m_spotLights; }
    size_t getMaxNumLights() const { return std::max(std::max(m_directionalLights.size(), m_pointLights.size()), m_spotLights.size()); }

private:
    std::vector<DirectionalLight> m_directionalLights;
    std::vector<PointLight> m_pointLights;
    std::vector<SpotLight> m_spotLights;

};

class PBRLightManager
{
public:
	PBRLightManager() = default;
	PBRLightManager(std::vector<PBRDirectionalLight> dirLights, std::vector<PBRPointLight> pointLights, std::vector<PBRSpotLight> spotLights);
	void lightGUI(const vg::BufferInfo& dirLightBuffer, const vg::BufferInfo& pointLightBuffer,
		const vg::BufferInfo& spotLightBuffer, const bool showRT = false) const;
	const std::vector<PBRDirectionalLight>& getDirectionalLights() const { return m_directionalLights; }
	const std::vector<PBRPointLight>& getPointLights() const { return m_pointLights; }
	const std::vector<PBRSpotLight>& getSpotLights() const { return m_spotLights; }
	size_t getMaxNumLights() const { return std::max(std::max(m_directionalLights.size(), m_pointLights.size()), m_spotLights.size()); }

private:
	std::vector<PBRDirectionalLight> m_directionalLights;
	std::vector<PBRPointLight> m_pointLights;
	std::vector<PBRSpotLight> m_spotLights;
};
