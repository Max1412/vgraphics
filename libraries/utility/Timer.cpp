#include "Timer.h"
#include "imgui/imgui.h"

//TODO maybe let the timer create the pool, so only a timer object is needed
void Timer::acquireCurrentTimestamp(const vk::Device& device, const vk::QueryPool& pool)
{
    auto res = device.getQueryPoolResults(pool, m_queryIndex, 1, 8, &m_currentTimestamp, 8, vk::QueryResultFlagBits::eWait | vk::QueryResultFlagBits::e64);
    if (res != vk::Result::eSuccess)
        throw std::runtime_error("Query not successful");

    // save elapsed time
    m_timeDiffs.push_back(static_cast<float>((m_currentTimestamp - m_lastTimestamp) / 1'000'000.0));

    // max 1000 time diffs
    if (m_timeDiffs.size() > 1000)
        m_timeDiffs.erase(m_timeDiffs.begin());

    m_lastTimestamp = m_currentTimestamp;
}

void Timer::CmdWriteTimestamp(const vk::CommandBuffer& cmdBuffer, const vk::PipelineStageFlagBits& stageflags,
    const vk::QueryPool& pool)
{
    cmdBuffer.writeTimestamp(stageflags, pool, m_queryIndex);
}

void Timer::drawGUIWindow()
{
    ImGui::SetNextWindowSize(ImVec2(300, 100), ImGuiSetCond_FirstUseEver);
    ImGui::Begin("Performance");
    float acc = 0;
    if (m_timeDiffs.size() > 21)
    {
        for (auto i = m_timeDiffs.size() - 21; i < m_timeDiffs.size(); ++i)
        {
            acc += m_timeDiffs.at(i);
        }
        acc /= 20.0f;
    }
    
    ImGui::PlotLines("Frametime", m_timeDiffs.data(), static_cast<int>(m_timeDiffs.size()), 0, nullptr, 0.0f, std::numeric_limits<float>::max());
    ImGui::Value("Frametime (milliseconds)", acc);
   
    ImGui::End();
}

void Timer::drawGUI()
{
    float acc = 0;
    if (m_timeDiffs.size() > 21)
    {
        for (auto i = m_timeDiffs.size() - 21; i < m_timeDiffs.size(); ++i)
        {
            acc += m_timeDiffs.at(i);
        }
        acc /= 20.0f;
    }

    const float availableWidth = ImGui::GetContentRegionAvailWidth();
    if (availableWidth >= 300)
        ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - 300);
    else if (availableWidth >= 70)
        ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - 70);

    ImGui::PushItemWidth(70);
    ImGui::Text("%.3f ms", acc);

    if (availableWidth >= 300)
    {
        ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - 230);
        ImGui::PushItemWidth(240);
        const auto offset = static_cast<int>(m_timeDiffs.size() <= 240 ? m_timeDiffs.size() - 1 : 240);
        if(!m_timeDiffs.empty())
            ImGui::PlotLines("", &m_timeDiffs.back() - offset, offset, 0, nullptr, 0.0f, std::numeric_limits<float>::max());
    }
}