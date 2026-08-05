#pragma once

#include "elm_overlayframe.hpp"
#include "tune.h"

#include <array>
#include <tesla.hpp>

class QlaunchSceneDiagnosticsGui final : public tsl::Gui {
  private:
    SysTuneOverlayFrame* m_frame{};
    tsl::elm::ListItem* m_status{};
    tsl::elm::ListItem* m_result{};
    tsl::elm::ListItem* m_pid{};
    tsl::elm::ListItem* m_scene{};
    tsl::elm::ListItem* m_queries{};
    tsl::elm::ListItem* m_requests{};
    tsl::elm::ListItem* m_context_commands{};
    tsl::elm::ListItem* m_scene_updates{};
    tsl::elm::ListItem* m_last_command{};
    std::array<tsl::elm::ListItem*, TuneQlaunchSceneHistorySize> m_history{};
    u8 m_tick{};

  public:
    tsl::elm::Element* createUI() final;
    void update() final;

  private:
    void refresh();
};
