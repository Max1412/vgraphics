#include "lightmanager.h"
#include "graphic/BaseApp.h"
#include "imgui/imgui.h"
#include <glm/gtc/type_ptr.hpp>
#include <charconv>

LightManager::LightManager(std::vector<DirectionalLight> dirLights, std::vector<PointLight> pointLights, std::vector<SpotLight> spotLights) :
    m_directionalLights(std::move(dirLights)), m_pointLights(std::move(pointLights)), m_spotLights(std::move(spotLights))
{
}

void LightManager::lightGUI(const vg::BufferInfo& dirLightBuffer, const vg::BufferInfo& pointLightBuffer, const vg::BufferInfo& spotLightBuffer) const
{
    if (ImGui::BeginMenu("Lights"))
    {
        std::string dirName("Directional Light  ");
        std::string pointName("Point Light  ");
        std::string spotName("Spot Light  ");

        for (size_t i = 0; i < m_directionalLights.size(); i++)
        {
            dirName.replace(dirName.size() - 1, 1, std::to_string(i));
            if (ImGui::CollapsingHeader(dirName.c_str()))
            {
                DirectionalLight* currentLight = reinterpret_cast<DirectionalLight*>(dirLightBuffer.m_BufferAllocInfo.pMappedData) + i;
                if (ImGui::DragFloat3((std::string("Intensity ") + std::to_string(i)).c_str(), glm::value_ptr(currentLight->intensity))) {}
                if (ImGui::SliderFloat3((std::string("Direction ") + std::to_string(i)).c_str(), glm::value_ptr(currentLight->direction), -1.0f, 1.0f)) {}
            }
        }
        for (size_t i = 0; i < m_pointLights.size(); i++)
        {
            pointName.replace(pointName.size() - 1, 1, std::to_string(i));
            if (ImGui::CollapsingHeader(pointName.c_str()))
            {
                PointLight* currentLight = reinterpret_cast<PointLight*>(pointLightBuffer.m_BufferAllocInfo.pMappedData) + i;
                if (ImGui::DragFloat3((std::string("Point Intensity ") + std::to_string(i)).c_str(), glm::value_ptr(currentLight->intensity))) {}
                if (ImGui::DragFloat3((std::string("Point Position ") + std::to_string(i)).c_str(), glm::value_ptr(currentLight->position))) {}
                if (ImGui::SliderFloat((std::string("Point Constant ") + std::to_string(i)).c_str(), &currentLight->constant, 0.0f, 1.0f)) {}
                if (ImGui::SliderFloat((std::string("Point Linear ") + std::to_string(i)).c_str(), &currentLight->linear, 0.0f, 0.25f)) {}
                if (ImGui::SliderFloat((std::string("Point Quadratic ") + std::to_string(i)).c_str(), &currentLight->quadratic, 0.0f, 0.1f)) {}
                if (ImGui::DragFloat((std::string("Point Radius ") + std::to_string(i)).c_str(), &currentLight->radius, 0.25f, 0.0f, 100.0f)) {}

            }
        }
        for (size_t i = 0; i < m_spotLights.size(); i++)
        {
            spotName.replace(spotName.size() - 1, 1, std::to_string(i));
            if (ImGui::CollapsingHeader(spotName.c_str()))
            {
                SpotLight* currentLight = reinterpret_cast<SpotLight*>(spotLightBuffer.m_BufferAllocInfo.pMappedData) + i;
                if (ImGui::DragFloat3((std::string("Spot Intensity ") + std::to_string(i)).c_str(), glm::value_ptr(currentLight->intensity))) {}
                if (ImGui::DragFloat3((std::string("Spot Position ") + std::to_string(i)).c_str(), glm::value_ptr(currentLight->position))) {}
                if (ImGui::SliderFloat3((std::string("Spot Direction ") + std::to_string(i)).c_str(), glm::value_ptr(currentLight->direction), -1.0f, 1.0f)) {}
                if (ImGui::SliderFloat((std::string("Spot Constant ") + std::to_string(i)).c_str(), &currentLight->constant, 0.0f, 1.0f)) {}
                if (ImGui::SliderFloat((std::string("Spot Linear ") + std::to_string(i)).c_str(), &currentLight->linear, 0.0f, 0.25f)) {}
                if (ImGui::SliderFloat((std::string("Spot Quadratic ") + std::to_string(i)).c_str(), &currentLight->quadratic, 0.0f, 0.1f)) {}
                if (ImGui::SliderFloat((std::string("Spot Cutoff ") + std::to_string(i)).c_str(), &currentLight->cutoff, 0.0f, glm::radians(90.0f))) {}
                if (ImGui::SliderFloat((std::string("Spot Outer Cutoff ") + std::to_string(i)).c_str(), &currentLight->outerCutoff, 0.0f, glm::radians(90.0f))) {}
                if (ImGui::DragFloat((std::string("Spot Radius ") + std::to_string(i)).c_str(), &currentLight->radius, 0.25f, 0.0f, 100.0f)) {}

            }
        }
        ImGui::EndMenu();
    }
}
