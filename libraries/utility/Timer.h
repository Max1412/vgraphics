#pragma once
#include <cstdint>
#include <vulkan/vulkan.hpp>
#include "graphic/Context.h"
#include <map>
#include "imgui/imgui.h"

class Timer
{
public:
    void acquireCurrentTimestamp(const vk::Device& device, const vk::QueryPool& pool);
    void acquireTimestepDifference(const vk::Device& device, const vk::QueryPool& pool);
    void cmdWriteTimestampStart(const vk::CommandBuffer& cmdBuffer, const vk::PipelineStageFlagBits& stageflags, const vk::QueryPool& pool) const;
    void cmdWriteTimestampStop(const vk::CommandBuffer& cmdBuffer, const vk::PipelineStageFlagBits& stageflags,
                               const vk::QueryPool& pool) const;
    void drawGUIWindow();
    void drawGUI();
    void incrementTimestamps() { m_timestampsPerFrameWritten++; }
    void resetTimestamps() { m_timestampsPerFrameWritten = 0; }
    uint64_t getCurrentNumberOfTimestampsPerFrame() const { return m_timestampsPerFrameWritten; }
    void setQueryIndex(int32_t index) { m_queryIndex = index; }

private:
    uint32_t m_numFramesToAccumulate = 20;
    uint32_t m_maxTimeDiffs = 1000;
    uint32_t m_queryIndex = 0;
    uint64_t m_currentTimestamp = 0;
    uint64_t m_lastTimestamp = 0;
    std::vector<float> m_timeDiffs;
    uint64_t m_timestampsPerFrameWritten = 0;
};

class TimerManager
{
public:
    TimerManager(std::map<std::string, Timer> timers, const vg::Context& context)
        : m_timers(std::move(timers)), m_context(context)
    {
        int32_t index = 0;
        for (auto& [name, timer] : m_timers)
        {
            timer.setQueryIndex(index);
            index +=2;
        }

        const vk::QueryPoolCreateInfo qpinfo({}, vk::QueryType::eTimestamp, 2 * static_cast<uint32_t>(m_timers.size()));
        m_queryPool = context.getDevice().createQueryPool(qpinfo);
    }

    ~TimerManager()
    {
        m_context.get().getDevice().destroyQueryPool(m_queryPool);
    }

    void writeTimestampStart(const std::string& timerName, const vk::CommandBuffer& cmdBuffer, const vk::PipelineStageFlagBits& stageflags)
    {
        Timer& currentTimer = m_timers.at(timerName);
        currentTimer.incrementTimestamps();
        currentTimer.cmdWriteTimestampStart(cmdBuffer, stageflags, m_queryPool);
    }

    void writeTimestampStop(const std::string& timerName, const vk::CommandBuffer& cmdBuffer, const vk::PipelineStageFlagBits& stageflags)
    {
        Timer& currentTimer = m_timers.at(timerName);
        currentTimer.incrementTimestamps();
        currentTimer.cmdWriteTimestampStop(cmdBuffer, stageflags, m_queryPool);
    }

    void querySpecificTimerResults(const std::string& timerName)
    {
        Timer& currentTimer = m_timers.at(timerName);
        queryTimerResult(currentTimer, timerName);
    }

    void queryAllTimerResults()
    {
        for(auto& [name, timer] : m_timers)
        {
            queryTimerResult(timer, name);
        }
    }

    void drawTimerGUIs()
    {
        for (auto& [name, timer] : m_timers)
        {
            ImGui::Text(name.c_str());
            ImGui::SameLine();
            timer.drawGUI();
        }
    }

    [[nodiscard]] const Timer& getTimer(const std::string& timerName) const
    {
        return m_timers.at(timerName);
    }

    [[nodiscard]] const std::map<std::string, Timer>& getTimers() const
    {
        return m_timers;
    }

private:

    void queryTimerResult(Timer& timer, const std::string& name) const
    {
        if constexpr (vg::enableValidationLayers)
        {
            if (const auto numTimeStamps = timer.getCurrentNumberOfTimestampsPerFrame(); numTimeStamps % 2 != 0)
                m_context.get().getLogger()->warn("Timer \"{}\" has an odd number ({}) of registered Timestamps before querying", name, numTimeStamps);
        }

        timer.acquireTimestepDifference(m_context.get().getDevice(), m_queryPool);
        timer.resetTimestamps();
    }

    std::map<std::string, Timer> m_timers;
    vk::QueryPool m_queryPool;
    std::reference_wrapper<const vg::Context> m_context;
    
};