#include "gui_ui_activity_diagnostics.hpp"

#include "elm_overlayframe.hpp"
#include "elm_text_block.hpp"
#include "tune.h"

#include <cstdio>
#include <string>

namespace {

const char* StatusText(u32 status) {
    switch (status) {
        case TuneUiActivityObserverStatus_Unavailable:
            return "Unavailable";
        case TuneUiActivityObserverStatus_Active:
            return "Active";
        case TuneUiActivityObserverStatus_Failed:
            return "Failed";
        default:
            return "Unknown";
    }
}

const char* EventTypeText(u32 type) {
    switch (type) {
        case PdmAppletEventType_Launch:
            return "Launch";
        case PdmAppletEventType_Exit:
            return "Exit";
        case PdmAppletEventType_InFocus:
            return "In Focus";
        case PdmAppletEventType_OutOfFocus:
            return "Out of Focus (3)";
        case PdmAppletEventType_OutOfFocus4:
            return "Background (4)";
        case PdmAppletEventType_Exit5:
        case PdmAppletEventType_Exit6:
            return "Exit";
        default:
            return "Other";
    }
}

const char* AppletText(u32 applet_id) {
    switch (applet_id) {
        case AppletId_application:
            return "Application";
        case AppletId_OverlayApplet:
            return "Overlay / Launch UI";
        default:
            return "Other";
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

std::string HexText(u64 value) {
    char text[24]{};
    std::snprintf(text, sizeof(text), "%016lX", value);
    return text;
}

void SetValue(
    tsl::elm::ListItem* item, const std::string& value, bool faint = false) {
    if (item->getValue() != value) {
        item->setValue(value, faint);
    }
}

}

tsl::elm::Element* UiActivityDiagnosticsGui::createUI() {
    auto frame = new SysTuneOverlayFrame();
    auto list = new tsl::elm::List();

    list->addItem(new tsl::elm::CategoryHeader("UI Activity Observer"));
    list->addItem(new ElmTextBlock(
        "Tracks HOME input, applet focus, and\n"
        "the application launch animation."));
    m_status = new tsl::elm::ListItem("Observer Status");
    m_result = new tsl::elm::ListItem("Last Result");
    list->addItem(m_status);
    list->addItem(m_result);

    m_input_status = new tsl::elm::ListItem("Input Status");
    m_input_result = new tsl::elm::ListItem("Input Result");
    list->addItem(m_input_status);
    list->addItem(m_input_result);

    auto reset = new tsl::elm::ListItem("Reset Signal History");
    reset->setClickListener([this, frame](u64 keys) {
        if (!(keys & HidNpadButton_A)) {
            return false;
        }
        const auto result = tuneResetUiActivityHistory();
        if (R_SUCCEEDED(result)) {
            frame->setToast("Signal History Reset", "Ready for a clean test");
        } else {
            frame->setToast("Reset Failed", ResultText(result));
        }
        refresh();
        return true;
    });
    list->addItem(reset);

    list->addItem(new tsl::elm::CategoryHeader("Quick Settings"));
    m_quick_settings_signal = new tsl::elm::ListItem("HOME Signal");
    m_home_held = new tsl::elm::ListItem("HOME Button");
    m_home_short_presses = new tsl::elm::ListItem("Short Presses");
    m_home_long_presses = new tsl::elm::ListItem("Long Presses");
    m_quick_settings = new tsl::elm::ListItem("Panel State");
    m_quick_settings_opens = new tsl::elm::ListItem("Open Signals");
    m_quick_settings_closes = new tsl::elm::ListItem("Close Signals");
    m_quick_settings_home_closes = new tsl::elm::ListItem("HOME Exits");
    m_quick_settings_b_closes = new tsl::elm::ListItem("B Exits");
    m_quick_settings_touch_closes = new tsl::elm::ListItem("Touch Exits");
    list->addItem(m_quick_settings_signal);
    list->addItem(m_home_held);
    list->addItem(m_home_short_presses);
    list->addItem(m_home_long_presses);
    list->addItem(m_quick_settings);
    list->addItem(m_quick_settings_opens);
    list->addItem(m_quick_settings_closes);
    list->addItem(m_quick_settings_home_closes);
    list->addItem(m_quick_settings_b_closes);
    list->addItem(m_quick_settings_touch_closes);

    list->addItem(new tsl::elm::CategoryHeader("Loading Screen"));
    m_loading_signal = new tsl::elm::ListItem("Application Signal");
    m_loading_overlay_signal = new tsl::elm::ListItem("Launch UI Signal");
    m_loading_overlay = new tsl::elm::ListItem("Launch UI Lifetime");
    m_loading = new tsl::elm::ListItem("Loading State");
    m_handoff = new tsl::elm::ListItem("In-Game Handoff");
    m_loading_starts = new tsl::elm::ListItem("Loading Starts");
    m_loading_ends = new tsl::elm::ListItem("Loading Ends");
    m_application_pid = new tsl::elm::ListItem("Application PID");
    m_application_program = new tsl::elm::ListItem("Application Program");
    list->addItem(m_loading_signal);
    list->addItem(m_loading_overlay_signal);
    list->addItem(m_loading_overlay);
    list->addItem(m_loading);
    list->addItem(m_handoff);
    list->addItem(m_loading_starts);
    list->addItem(m_loading_ends);
    list->addItem(m_application_pid);
    list->addItem(m_application_program);

    list->addItem(new tsl::elm::CategoryHeader("Application Events"));
    m_application_launches = new tsl::elm::ListItem("Launch (0)");
    m_application_focuses = new tsl::elm::ListItem("In Focus (2)");
    m_application_out_of_focus = new tsl::elm::ListItem("Out of Focus (3)");
    m_application_background = new tsl::elm::ListItem("Background (4)");
    m_application_exits = new tsl::elm::ListItem("Exit (1/5/6)");
    list->addItem(m_application_launches);
    list->addItem(m_application_focuses);
    list->addItem(m_application_out_of_focus);
    list->addItem(m_application_background);
    list->addItem(m_application_exits);

    list->addItem(new tsl::elm::CategoryHeader("Library Applet Focus"));
    m_library_signal = new tsl::elm::ListItem("Signal");
    m_library_foreground = new tsl::elm::ListItem("Visible State");
    m_library_program = new tsl::elm::ListItem("Latest Program");
    m_library_focuses = new tsl::elm::ListItem("Focus Events");
    m_library_out_of_focus = new tsl::elm::ListItem("Background Events");
    list->addItem(m_library_signal);
    list->addItem(m_library_foreground);
    list->addItem(m_library_program);
    list->addItem(m_library_focuses);
    list->addItem(m_library_out_of_focus);

    list->addItem(new tsl::elm::CategoryHeader("Latest PDM Event"));
    m_latest_type = new tsl::elm::ListItem("Event Type");
    m_latest_applet = new tsl::elm::ListItem("Applet");
    m_latest_program = new tsl::elm::ListItem("Program");
    m_latest_index = new tsl::elm::ListItem("Event Index");
    m_queries = new tsl::elm::ListItem("Queries");
    m_events = new tsl::elm::ListItem("Events Read");
    list->addItem(m_latest_type);
    list->addItem(m_latest_applet);
    list->addItem(m_latest_program);
    list->addItem(m_latest_index);
    list->addItem(m_queries);
    list->addItem(m_events);

    frame->setDescription("\uE0E1 Back   \uE0E0 Reset History");
    frame->setContent(list);
    refresh();
    return frame;
}

void UiActivityDiagnosticsGui::refresh() {
    TuneUiActivityObserverInfo info{};
    const auto result = tuneGetUiActivityObserver(&info);
    if (R_FAILED(result)) {
        SetValue(m_status, "IPC Error", true);
        SetValue(m_result, ResultText(result));
        return;
    }

    SetValue(m_status, StatusText(info.status),
        info.status != TuneUiActivityObserverStatus_Active);
    SetValue(m_result, ResultText(info.last_result),
        R_SUCCEEDED(info.last_result));
    SetValue(m_input_status, StatusText(info.input_status),
        info.input_status != TuneUiActivityObserverStatus_Active);
    SetValue(m_input_result, ResultText(info.input_last_result),
        R_SUCCEEDED(info.input_last_result));
    SetValue(m_quick_settings_signal,
        info.has_home_button_signal ? "Seen" : "Waiting",
        !info.has_home_button_signal);
    SetValue(m_home_held,
        info.home_button_held ? "Held" : "Released");
    SetValue(m_home_short_presses,
        std::to_string(info.home_short_press_count));
    SetValue(m_home_long_presses,
        std::to_string(info.home_long_press_count));
    SetValue(m_quick_settings,
        info.quick_settings_open ? "Open" : "Closed");
    SetValue(m_quick_settings_opens,
        std::to_string(info.quick_settings_open_count));
    SetValue(m_quick_settings_closes,
        std::to_string(info.quick_settings_close_count));
    SetValue(m_quick_settings_home_closes,
        std::to_string(info.quick_settings_home_close_count));
    SetValue(m_quick_settings_b_closes,
        std::to_string(info.quick_settings_b_close_count));
    SetValue(m_quick_settings_touch_closes,
        std::to_string(info.quick_settings_touch_close_count));
    SetValue(m_loading_signal,
        info.has_application_signal ? "Seen" : "Waiting",
        !info.has_application_signal);
    SetValue(m_loading_overlay_signal,
        info.has_overlay_signal ? "Seen" : "Waiting",
        !info.has_overlay_signal);
    SetValue(m_loading_overlay,
        info.loading_overlay_active ? "Holding" : "Inactive");
    SetValue(m_loading, info.loading_active ? "Active" : "Inactive");
    SetValue(m_handoff,
        info.application_handoff_active ? "Silent" : "Inactive");
    SetValue(m_loading_starts,
        std::to_string(info.loading_start_count));
    SetValue(m_loading_ends,
        std::to_string(info.loading_end_count));
    SetValue(m_application_pid,
        info.application_process_id
            ? std::to_string(info.application_process_id) : "None",
        info.application_process_id == 0);
    SetValue(m_application_program,
        info.application_program_id ? HexText(info.application_program_id) : "None",
        info.application_program_id == 0);
    SetValue(m_application_launches,
        std::to_string(info.application_launch_count));
    SetValue(m_application_focuses,
        std::to_string(info.application_in_focus_count));
    SetValue(m_application_out_of_focus,
        std::to_string(info.application_out_of_focus_count));
    SetValue(m_application_background,
        std::to_string(info.application_background_count));
    SetValue(m_application_exits,
        std::to_string(info.application_exit_count));
    SetValue(m_library_signal,
        info.has_library_applet_signal ? "Seen" : "Waiting",
        !info.has_library_applet_signal);
    SetValue(m_library_foreground,
        info.library_applet_foreground ? "Applet" : "Home / Other");
    SetValue(m_library_program,
        info.last_library_applet_program_id
            ? HexText(info.last_library_applet_program_id) : "None",
        info.last_library_applet_program_id == 0);
    SetValue(m_library_focuses,
        std::to_string(info.library_applet_in_focus_count));
    SetValue(m_library_out_of_focus,
        std::to_string(info.library_applet_out_of_focus_count));
    SetValue(m_latest_type,
        info.has_last_event ? EventTypeText(info.last_event_type) : "Not Seen",
        !info.has_last_event);
    SetValue(m_latest_applet,
        info.has_last_event ? AppletText(info.last_applet_id) : "Not Seen",
        !info.has_last_event);
    SetValue(m_latest_program,
        info.has_last_event ? HexText(info.last_program_id) : "Not Seen",
        !info.has_last_event);
    SetValue(m_latest_index,
        info.has_last_event ? std::to_string(info.last_event_index) : "Not Seen",
        !info.has_last_event);
    SetValue(m_queries, std::to_string(info.query_count));
    SetValue(m_events, std::to_string(info.event_count));
}

void UiActivityDiagnosticsGui::update() {
    if ((m_tick++ % 15) == 0) {
        refresh();
    }
}
