#pragma once

#include "elm_overlayframe.hpp"

#include <tesla.hpp>

class OstPlaylistGui final : public tsl::Gui {
  private:
    u64 m_title_id{};
    std::string m_name;
    bool m_startup{};
    SysTuneOverlayFrame* m_frame{};
    tsl::elm::List* m_list{};
    u32 m_known_count{};
    bool m_rebuild_pending{};

  public:
    OstPlaylistGui(u64 title_id, std::string name, bool startup);
    tsl::elm::Element* createUI() final;
    void update() final;

  private:
    void populate();
    void requestRebuild();
};
