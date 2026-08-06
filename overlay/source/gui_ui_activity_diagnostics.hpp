#pragma once

#include <tesla.hpp>

class UiActivityDiagnosticsGui final : public tsl::Gui {
  public:
    tsl::elm::Element* createUI() final;
    void update() final;

  private:
    void refresh();

    tsl::elm::ListItem* m_status{};
    tsl::elm::ListItem* m_result{};
    tsl::elm::ListItem* m_input_status{};
    tsl::elm::ListItem* m_input_result{};
    tsl::elm::ListItem* m_quick_settings_signal{};
    tsl::elm::ListItem* m_home_held{};
    tsl::elm::ListItem* m_home_short_presses{};
    tsl::elm::ListItem* m_home_long_presses{};
    tsl::elm::ListItem* m_quick_settings{};
    tsl::elm::ListItem* m_quick_settings_opens{};
    tsl::elm::ListItem* m_quick_settings_closes{};
    tsl::elm::ListItem* m_quick_settings_home_closes{};
    tsl::elm::ListItem* m_quick_settings_b_closes{};
    tsl::elm::ListItem* m_quick_settings_touch_closes{};
    tsl::elm::ListItem* m_loading_signal{};
    tsl::elm::ListItem* m_loading_overlay_signal{};
    tsl::elm::ListItem* m_loading{};
    tsl::elm::ListItem* m_handoff{};
    tsl::elm::ListItem* m_loading_starts{};
    tsl::elm::ListItem* m_loading_ends{};
    tsl::elm::ListItem* m_application_pid{};
    tsl::elm::ListItem* m_application_program{};
    tsl::elm::ListItem* m_application_launches{};
    tsl::elm::ListItem* m_application_focuses{};
    tsl::elm::ListItem* m_application_out_of_focus{};
    tsl::elm::ListItem* m_application_background{};
    tsl::elm::ListItem* m_application_exits{};
    tsl::elm::ListItem* m_library_signal{};
    tsl::elm::ListItem* m_library_foreground{};
    tsl::elm::ListItem* m_library_program{};
    tsl::elm::ListItem* m_library_focuses{};
    tsl::elm::ListItem* m_library_out_of_focus{};
    tsl::elm::ListItem* m_latest_type{};
    tsl::elm::ListItem* m_latest_applet{};
    tsl::elm::ListItem* m_latest_program{};
    tsl::elm::ListItem* m_latest_index{};
    tsl::elm::ListItem* m_queries{};
    tsl::elm::ListItem* m_events{};
    u32 m_tick{};
};
