#include "gui_power_state_diagnostics.hpp"

#include "elm_overlayframe.hpp"
#include "elm_text_block.hpp"
#include "tune.h"

#include <cstdio>
#include <string>

namespace {

const char* StatusText(u32 status) {
    switch (status) {
        case TunePowerObserverStatus_Unavailable:
            return "Unavailable";
        case TunePowerObserverStatus_Starting:
            return "Starting";
        case TunePowerObserverStatus_Active:
            return "Active";
        case TunePowerObserverStatus_Failed:
            return "Failed";
        default:
            return "Unknown";
    }
}

const char* StateText(u32 state) {
    switch (state) {
        case 0:
            return "Full Awake (0)";
        case 1:
            return "Minimum Awake (1)";
        case 2:
            return "Sleep Ready (2)";
        case 3:
            return "Essential Sleep Ready (3)";
        case 4:
            return "Essential Awake (4)";
        case 5:
            return "Shutdown Ready (5)";
        default:
            return "Unknown";
    }
}

const char* TransitionText(u8 transition) {
    switch (transition) {
        case TunePowerTransition_Sleep:
            return "Sleep / Display Off";
        case TunePowerTransition_Wake:
            return "Wake";
        default:
            return "Not Seen";
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

std::string ModuleText(u32 module_id) {
    char text[20]{};
    std::snprintf(text, sizeof(text), "0x%02X (%u)", module_id, module_id);
    return text;
}

void SetValue(
    tsl::elm::ListItem* item, const std::string& value, bool faint = false) {
    if (item->getValue() != value) {
        item->setValue(value, faint);
    }
}

}

tsl::elm::Element* PowerStateDiagnosticsGui::createUI() {
    auto frame = new SysTuneOverlayFrame();
    auto list = new tsl::elm::List();

    list->addItem(new tsl::elm::CategoryHeader("Power-State Observer"));
    list->addItem(new ElmTextBlock(
        "Receives sleep/wake before qlaunch changes.\n"
        "Queued OST audio is flushed first."));

    m_status = new tsl::elm::ListItem("Observer Status");
    m_result = new tsl::elm::ListItem("Last Result");
    m_module = new tsl::elm::ListItem("PSC Module");
    m_state = new tsl::elm::ListItem("Current Power State");
    m_transition = new tsl::elm::ListItem("Last Transition");
    m_lock_screen = new tsl::elm::ListItem("Lock Screen Expected");
    m_audio_hold = new tsl::elm::ListItem("Audio Safety Hold");
    list->addItem(m_status);
    list->addItem(m_result);
    list->addItem(m_module);
    list->addItem(m_state);
    list->addItem(m_transition);
    list->addItem(m_lock_screen);
    list->addItem(m_audio_hold);

    list->addItem(new tsl::elm::CategoryHeader("Signal Counters"));
    m_requests = new tsl::elm::ListItem("Power Requests");
    m_transitions = new tsl::elm::ListItem("Detected Transitions");
    m_sleeps = new tsl::elm::ListItem("Sleep / Off Signals");
    m_wakes = new tsl::elm::ListItem("Wake Signals");
    m_timeouts = new tsl::elm::ListItem("Audio Flush Timeouts");
    list->addItem(m_requests);
    list->addItem(m_transitions);
    list->addItem(m_sleeps);
    list->addItem(m_wakes);
    list->addItem(m_timeouts);

    frame->setDescription("\uE0E1 Back");
    frame->setContent(list);
    refresh();
    return frame;
}

void PowerStateDiagnosticsGui::refresh() {
    TunePowerStateObserverInfo info{};
    const auto result = tuneGetPowerStateObserver(&info);
    if (R_FAILED(result)) {
        SetValue(m_status, "IPC Error", true);
        SetValue(m_result, ResultText(result));
        return;
    }

    SetValue(m_status, StatusText(info.status),
        info.status == TunePowerObserverStatus_Unavailable ||
        info.status == TunePowerObserverStatus_Failed);
    SetValue(m_result, ResultText(info.last_result),
        R_SUCCEEDED(info.last_result));
    SetValue(m_module, ModuleText(info.module_id));
    SetValue(m_state,
        info.has_state ? StateText(info.current_state) : "Not Seen",
        !info.has_state);
    SetValue(m_transition, TransitionText(info.last_transition),
        info.last_transition == TunePowerTransition_None);
    SetValue(m_lock_screen,
        info.lock_screen_enabled ? "Yes" : "No");
    SetValue(m_audio_hold,
        info.audio_hold_active ? "Active" : "Released");
    SetValue(m_requests, std::to_string(info.request_count));
    SetValue(m_transitions, std::to_string(info.transition_count));
    SetValue(m_sleeps, std::to_string(info.sleep_count));
    SetValue(m_wakes, std::to_string(info.wake_count));
    SetValue(m_timeouts,
        std::to_string(info.audio_quiesce_timeout_count),
        info.audio_quiesce_timeout_count != 0);
}

void PowerStateDiagnosticsGui::update() {
    if ((m_tick++ % 15) == 0) {
        refresh();
    }
}
