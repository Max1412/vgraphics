#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace vg {
    struct BufferInfo;
}

struct DirectionalLight
{
    glm::vec3 direction;
    int32_t pad0 = 0;
    glm::vec3 intensity;
    int32_t pad1 = 0;

};

struct PointLight
{
    glm::vec3 position;
    float constant;
    glm::vec3 intensity;
    float linear;
    float quadratic;
    float radius;
    int32_t pad0 = 0, pad1 = 0;

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
    int32_t pad0 = 0, pad1 = 0;
};

class LightManager
{
public:
    LightManager() = default;
    LightManager(std::vector<DirectionalLight> dirLights, std::vector<PointLight> pointLights, std::vector<SpotLight> spotLights);
    void lightGUI(const vg::BufferInfo& dirLightBuffer, const vg::BufferInfo& pointLightBuffer,
                  const vg::BufferInfo& spotLightBuffer) const;
    const std::vector<DirectionalLight>& getDirectionalLights() const { return m_directionalLights; }
    const std::vector<PointLight>& getPointLights() const { return m_pointLights; }
    const std::vector<SpotLight>& getSpotLights() const { return m_spotLights; }

private:
    std::vector<DirectionalLight> m_directionalLights;
    std::vector<PointLight> m_pointLights;
    std::vector<SpotLight> m_spotLights;

};
