#include "Timer.h"
#include "imgui/imgui.h"

//TODO maybe let the timer create the pool, so only a timer object is needed
void Timer::acquireCurrentTimestamp(const vk::Device& device, const vk::QueryPool& pool)
{
    auto res = device.getQueryPoolResults(pool, m_queryIndex, 1, sizeof(uint64_t), &m_currentTimestamp, sizeof(uint64_t), vk::QueryResultFlagBits::eWait | vk::QueryResultFlagBits::e64);
    if (res != vk::Result::eSuccess)
        throw std::runtime_error("Query not successful");

    // save elapsed time
    m_timeDiffs.push_back(static_cast<float>((m_currentTimestamp - m_lastTimestamp) / 1'000'000.0));

    // max amount of time diffs
    if (m_timeDiffs.size() > m_maxTimeDiffs)
        m_timeDiffs.erase(m_timeDiffs.begin());

    m_lastTimestamp = m_currentTimestamp;
}

void Timer::acquireTimestepDifference(const vk::Device& device, const vk::QueryPool& pool)
{
    auto res = device.getQueryPoolResults(pool, m_queryIndex, 1, sizeof(uint64_t), &m_lastTimestamp, sizeof(uint64_t), vk::QueryResultFlagBits::eWait | vk::QueryResultFlagBits::e64);
    if (res != vk::Result::eSuccess)
        throw std::runtime_error("Query not successful");

    res = device.getQueryPoolResults(pool, m_queryIndex + 1, 1, sizeof(uint64_t), &m_currentTimestamp, sizeof(uint64_t), vk::QueryResultFlagBits::eWait | vk::QueryResultFlagBits::e64);
    if (res != vk::Result::eSuccess)
        throw std::runtime_error("Query not successful");

    // save elapsed time
    m_timeDiffs.push_back(static_cast<float>((m_currentTimestamp - m_lastTimestamp) / 1'000'000.0));

    // max amount of time diffs
    if (m_timeDiffs.size() > m_maxTimeDiffs)
        m_timeDiffs.erase(m_timeDiffs.begin());
}

void Timer::cmdWriteTimestampStart(const vk::CommandBuffer& cmdBuffer, const vk::PipelineStageFlagBits& stageflags,
    const vk::QueryPool& pool) const
{
    cmdBuffer.writeTimestamp(stageflags, pool, m_queryIndex);
}

void Timer::cmdWriteTimestampStop(const vk::CommandBuffer& cmdBuffer, const vk::PipelineStageFlagBits& stageflags,
    const vk::QueryPool& pool) const
{
    cmdBuffer.writeTimestamp(stageflags, pool, m_queryIndex + 1);
}

void Timer::drawGUIWindow()
{
    ImGui::SetNextWindowSize(ImVec2(300, 100), ImGuiSetCond_FirstUseEver);
    ImGui::Begin("Performance");
    float acc = 0;
    if (m_timeDiffs.size() > m_numFramesToAccumulate + 1)
    {
        for (auto i = m_timeDiffs.size() - m_numFramesToAccumulate + 1; i < m_timeDiffs.size(); ++i)
        {
            acc += m_timeDiffs.at(i);
        }
        acc /= static_cast<float>(m_numFramesToAccumulate);
    }
    
    ImGui::PlotLines("Frametime", m_timeDiffs.data(), static_cast<int>(m_timeDiffs.size()), 0, nullptr, 0.0f, std::numeric_limits<float>::max());
    ImGui::Value("Frametime (milliseconds)", acc);
   
    ImGui::End();
}

void Timer::drawGUI()
{
    float acc = 0;
    if (m_timeDiffs.size() > m_numFramesToAccumulate + 1)
    {
        for (auto i = m_timeDiffs.size() - m_numFramesToAccumulate + 1; i < m_timeDiffs.size(); ++i)
        {
            acc += m_timeDiffs.at(i);
        }
        acc /= static_cast<float>(m_numFramesToAccumulate);
    }

    const float availableWidth = ImGui::GetContentRegionAvailWidth();
    if (availableWidth >= 300)
        ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - 300);
    else if (availableWidth >= 70)
        ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - 70);

    //ImGui::PushItemWidth(70);
    ImGui::Text("%.3f ms ", acc);

    if (availableWidth >= 300)
    {
        ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - 240);
        ImGui::PushItemWidth(240);
        const auto offset = static_cast<int>(m_timeDiffs.size() <= 240 ? m_timeDiffs.size() - 1 : 240);
        if(!m_timeDiffs.empty())
            ImGui::PlotLines("", &m_timeDiffs.back() - offset, offset, 0, nullptr, 0.0f, std::numeric_limits<float>::max());

        ImGui::PopItemWidth();
    }

    //ImGui::PopItemWidth();
}