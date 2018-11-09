#pragma once
#include <cstdint>
#include <vulkan/vulkan.hpp>

class Timer
{
public:
    void acquireCurrentTimestamp(const vk::Device& device, const vk::QueryPool& pool);
    void CmdWriteTimestamp(const vk::CommandBuffer& cmdBuffer, const vk::PipelineStageFlagBits& stageflags, const vk::QueryPool& pool);
    void drawGUIWindow();
private:
    uint32_t m_queryIndex = 0;
    uint64_t m_currentTimestamp = 0;
    uint64_t m_lastTimestamp = 0;
    std::vector<float> m_timeDiffs;
};
