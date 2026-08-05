#pragma once

#include <tesla.hpp>

class PowerStateDiagnosticsGui final : public tsl::Gui {
  public:
    tsl::elm::Element* createUI() final;
    void update() final;

  private:
    void refresh();

    tsl::elm::ListItem* m_status{};
    tsl::elm::ListItem* m_result{};
    tsl::elm::ListItem* m_module{};
    tsl::elm::ListItem* m_state{};
    tsl::elm::ListItem* m_transition{};
    tsl::elm::ListItem* m_lock_screen{};
    tsl::elm::ListItem* m_audio_hold{};
    tsl::elm::ListItem* m_requests{};
    tsl::elm::ListItem* m_transitions{};
    tsl::elm::ListItem* m_sleeps{};
    tsl::elm::ListItem* m_wakes{};
    tsl::elm::ListItem* m_timeouts{};
    u32 m_tick{};
};
