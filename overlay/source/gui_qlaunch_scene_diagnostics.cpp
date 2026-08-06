#include "gui_qlaunch_scene_diagnostics.hpp"

#include "elm_text_block.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

const char* StatusText(u32 status) {
    switch (status) {
        case TuneQlaunchObserverStatus_Unavailable:
            return "Unavailable";
        case TuneQlaunchObserverStatus_Starting:
            return "Starting";
        case TuneQlaunchObserverStatus_Installed:
            return "Installed — Waiting";
        case TuneQlaunchObserverStatus_QlaunchConnected:
            return "Qlaunch Connected";
        case TuneQlaunchObserverStatus_ReceivingScenes:
            return "Receiving Scenes";
        case TuneQlaunchObserverStatus_Failed:
            return "Failed";
        default:
            return "Unknown";
    }
}

std::string ResultText(Result result) {
    if (R_SUCCEEDED(result)) {
        return "Success";
    }

    char text[16]{};
    std::snprintf(
        text, sizeof(text), "2%03u-%04u",
        R_MODULE(result), R_DESCRIPTION(result));
    return text;
}

std::string ProcessIdText(u64 process_id) {
    if (process_id == 0) {
        return "Not Connected";
    }

    char text[24]{};
    std::snprintf(
        text, sizeof(text), "0x%016llX",
        static_cast<unsigned long long>(process_id));
    return text;
}

std::string SceneText(u8 scene) {
    char text[24]{};
    std::snprintf(
        text, sizeof(text), "0x%02X (%u)", scene, scene);
    return text;
}

std::string CommandText(u32 command_id) {
    char text[20]{};
    std::snprintf(text, sizeof(text), "0x%X (%u)", command_id, command_id);
    return text;
}

std::string DeltaText(const TuneQlaunchSceneEvent& current,
                      const TuneQlaunchSceneEvent* previous) {
    auto text = SceneText(current.scene);
    if (previous == nullptr || current.tick < previous->tick) {
        return text;
    }

    const auto milliseconds =
        armTicksToNs(current.tick - previous->tick) / 1'000'000ULL;
    return text + "  +" + std::to_string(milliseconds) + " ms";
}

void SetValue(
    tsl::elm::ListItem* item, const std::string& value, bool faint = false) {
    if (item->getValue() != value) {
        item->setValue(value, faint);
    }
}

}

tsl::elm::Element* QlaunchSceneDiagnosticsGui::createUI() {
    m_frame = new SysTuneOverlayFrame();
    auto list = new tsl::elm::List();

    list->addItem(new tsl::elm::CategoryHeader("Qlaunch Scene Observer"));
    list->addItem(new ElmTextBlock(
        "Reads qlaunch's raw scene telemetry.\n"
        "Open a screen, then compare its history."));

    m_status = new tsl::elm::ListItem("Observer Status");
    m_result = new tsl::elm::ListItem("Last Result");
    m_pid = new tsl::elm::ListItem("Qlaunch Process");
    m_scene = new tsl::elm::ListItem("Current Raw Scene");
    list->addItem(m_status);
    list->addItem(m_result);
    list->addItem(m_pid);
    list->addItem(m_scene);

    list->addItem(new tsl::elm::CategoryHeader("Signal Counters"));
    m_queries = new tsl::elm::ListItem("MITM Queries");
    m_requests = new tsl::elm::ListItem("Qlaunch Requests");
    m_context_commands = new tsl::elm::ListItem("Context Commands");
    m_scene_updates = new tsl::elm::ListItem("Scene Updates");
    m_last_command = new tsl::elm::ListItem("Last Command");
    list->addItem(m_queries);
    list->addItem(m_requests);
    list->addItem(m_context_commands);
    list->addItem(m_scene_updates);
    list->addItem(m_last_command);

    list->addItem(new tsl::elm::CategoryHeader("Raw Scene History"));
    auto reset = new tsl::elm::ListItem("Reset Scene History");
    reset->setClickListener([this](u64 keys) {
        if (!(keys & HidNpadButton_A)) {
            return false;
        }
        const auto result = tuneResetQlaunchSceneHistory();
        if (R_SUCCEEDED(result)) {
            m_frame->setToast("Scene History Reset", "Ready for a clean test");
        } else {
            m_frame->setToast("Reset Failed", ResultText(result));
        }
        refresh();
        return true;
    });
    list->addItem(reset);

    for (size_t i = 0; i < m_history.size(); ++i) {
        m_history[i] = new tsl::elm::ListItem(
            "Transition " + std::to_string(i + 1));
        list->addItem(m_history[i]);
    }

    m_frame->setDescription("\uE0E1 Back   \uE0E0 Reset History");
    m_frame->setContent(list);
    refresh();
    return m_frame;
}

void QlaunchSceneDiagnosticsGui::refresh() {
    TuneQlaunchSceneObserverInfo info{};
    const auto result = tuneGetQlaunchSceneObserver(&info);
    if (R_FAILED(result)) {
        SetValue(m_status, "IPC Error", true);
        SetValue(m_result, ResultText(result));
        return;
    }

    SetValue(m_status, StatusText(info.status),
        info.status == TuneQlaunchObserverStatus_Unavailable ||
        info.status == TuneQlaunchObserverStatus_Failed);
    SetValue(m_result, ResultText(info.last_result),
        R_SUCCEEDED(info.last_result));
    SetValue(m_pid, ProcessIdText(info.qlaunch_process_id),
        info.qlaunch_process_id == 0);
    SetValue(m_scene,
        info.has_scene ? SceneText(info.current_scene) : "Not Seen",
        !info.has_scene);
    SetValue(m_queries, std::to_string(info.query_count));
    SetValue(m_requests, std::to_string(info.request_count));
    SetValue(m_context_commands,
        std::to_string(info.context_command_count));
    SetValue(m_scene_updates, std::to_string(info.scene_update_count));
    SetValue(m_last_command,
        info.has_last_command ? CommandText(info.last_command_id) : "Not Seen",
        !info.has_last_command);

    const auto history_count = std::min<size_t>(
        info.history_count, TuneQlaunchSceneHistorySize);
    for (size_t i = 0; i < m_history.size(); ++i) {
        if (i < history_count) {
            const auto* previous = i == 0 ? nullptr : &info.history[i - 1];
            SetValue(m_history[i], DeltaText(info.history[i], previous));
        } else {
            SetValue(m_history[i], "Not Recorded", true);
        }
    }
}

void QlaunchSceneDiagnosticsGui::update() {
    if ((m_tick++ % 15) == 0) {
        refresh();
    }
}
