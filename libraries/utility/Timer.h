#pragma once
#include <cstdint>
#include <vulkan/vulkan.hpp>
#include "graphic/Context.h"
#include <map>
#include "imgui/imgui.h"

class Timer
{
public:
    Timer() = default;
    explicit Timer(const bool guiActive) : m_guiActive(guiActive){}
    void acquireCurrentTimestamp(const vk::Device& device, const vk::QueryPool& pool);
    void acquireTimestepDifference(const vk::Device& device, const vk::QueryPool& pool, const size_t frameIndex);
    void cmdWriteTimestampStart(const vk::CommandBuffer& cmdBuffer, const vk::PipelineStageFlagBits& stageflags, const vk::QueryPool& pool, const size_t frameIndex = 0) const;
    void cmdWriteTimestampStop(const vk::CommandBuffer& cmdBuffer, const vk::PipelineStageFlagBits& stageflags, const vk::QueryPool& pool, const size_t frameIndex = 0) const;
    void drawGUIWindow();
    void drawGUI();
    void incrementTimestamps() { m_timestampsPerFrameWritten++; }
    void resetTimestamps() { m_timestampsPerFrameWritten = 0; }
    [[nodiscard]] uint64_t getCurrentNumberOfTimestampsPerFrame() const { return m_timestampsPerFrameWritten; }
    void setQueryIndex(const int32_t index) { m_queryIndex = index; }
    [[nodiscard]] bool isGuiActive() const { return m_guiActive; }
    void setGuiActiveStatus(const bool status) { m_guiActive = status; }
    [[nodiscard]] const std::vector<float>& getTimeDiffs() const { return m_timeDiffs; }

private:
    uint32_t m_numFramesToAccumulate = 20U;
    uint32_t m_maxTimeDiffs = 1000U;
    uint32_t m_queryIndex = 0;
    uint64_t m_currentTimestamp = 0;
    uint64_t m_lastTimestamp = 0;
    bool m_guiActive = true;
    std::vector<float> m_timeDiffs;
    uint64_t m_timestampsPerFrameWritten = 0;
};

class TimerManager
{
public:
    TimerManager(std::map<std::string, Timer> timers, const vg::Context& context)
        : m_timers(std::move(timers)), m_context(context)
    {
        uint32_t index = 0;
        for (auto& [name, timer] : m_timers)
        {
            timer.setQueryIndex(index);
            index += 2 * static_cast<uint32_t>(context.getSwapChainImages().size());
        }

        const vk::QueryPoolCreateInfo qpinfo({}, vk::QueryType::eTimestamp, static_cast<uint32_t>(2 * context.getSwapChainImages().size() * m_timers.size()));
        m_queryPool = context.getDevice().createQueryPool(qpinfo);
    }

    ~TimerManager()
    {
        m_context.get().getDevice().destroyQueryPool(m_queryPool);
    }

    void writeTimestampStart(const std::string& timerName, const vk::CommandBuffer& cmdBuffer, const vk::PipelineStageFlagBits& stageflags, const size_t frameIndex)
    {
        Timer& currentTimer = m_timers.at(timerName);
        currentTimer.incrementTimestamps();
        currentTimer.cmdWriteTimestampStart(cmdBuffer, stageflags, m_queryPool, frameIndex);
    }

    void writeTimestampStop(const std::string& timerName, const vk::CommandBuffer& cmdBuffer, const vk::PipelineStageFlagBits& stageflags, const size_t frameIndex)
    {
        Timer& currentTimer = m_timers.at(timerName);
        currentTimer.incrementTimestamps();
        currentTimer.cmdWriteTimestampStop(cmdBuffer, stageflags, m_queryPool, frameIndex);
    }

    void querySpecificTimerResults(const std::string& timerName, const size_t frameIndex = 0)
    {
        Timer& currentTimer = m_timers.at(timerName);
        queryTimerResult(currentTimer, timerName, frameIndex);
    }

    void queryAllTimerResults(const size_t frameIndex)
    {
        for(auto& [name, timer] : m_timers)
        {
            queryTimerResult(timer, name, frameIndex);
        }
    }

    void drawTimerGUIs()
    {
        for (auto& [name, timer] : m_timers)
        {
            if(timer.isGuiActive())
            {
                ImGui::Text(name.c_str());
                ImGui::SameLine();
                timer.drawGUI();
            }
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

    void setGuiActiveStatusForTimer(const std::string& timerName, const bool status)
    {
        m_timers.at(timerName).setGuiActiveStatus(status);
    }

    void eraseTimer(const std::string& timerName)
    {
        m_timers.erase(timerName);
    }

private:

    void queryTimerResult(Timer& timer, const std::string& name, const size_t frameIndex) const
    {
        if constexpr (vg::enableValidationLayers)
        {
            if (const auto numTimeStamps = timer.getCurrentNumberOfTimestampsPerFrame(); numTimeStamps % 2 != 0)
                m_context.get().getLogger()->warn("Timer \"{}\" has an odd number ({}) of registered Timestamps before querying", name, numTimeStamps);
        }

        timer.acquireTimestepDifference(m_context.get().getDevice(), m_queryPool, frameIndex);
        timer.resetTimestamps();
    }

    std::map<std::string, Timer> m_timers;
    vk::QueryPool m_queryPool;
    std::reference_wrapper<const vg::Context> m_context;
    
};