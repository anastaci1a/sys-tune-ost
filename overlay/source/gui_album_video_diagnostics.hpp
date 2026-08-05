#pragma once

#include "elm_overlayframe.hpp"
#include "tune.h"

#include <tesla.hpp>

class AlbumVideoDiagnosticsGui final : public tsl::Gui {
  private:
    tsl::elm::ListItem* m_status{};
    tsl::elm::ListItem* m_result{};
    tsl::elm::ListItem* m_pid{};
    tsl::elm::ListItem* m_video_state{};
    tsl::elm::ListItem* m_active_streams{};
    tsl::elm::ListItem* m_queries{};
    tsl::elm::ListItem* m_requests{};
    tsl::elm::ListItem* m_opens{};
    tsl::elm::ListItem* m_reads{};
    tsl::elm::ListItem* m_closes{};
    tsl::elm::ListItem* m_state_updates{};
    tsl::elm::ListItem* m_last_command{};
    u8 m_tick{};

  public:
    tsl::elm::Element* createUI() final;
    void update() final;

  private:
    void refresh();
};
