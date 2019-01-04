#include "lightmanager.h"

LightManager::LightManager(std::vector<DirectionalLight> dirLights, std::vector<PointLight> pointLights, std::vector<SpotLight> spotLights) :
    m_directionalLights(std::move(dirLights)), m_pointLights(std::move(pointLights)), m_spotLights(std::move(spotLights))
{
}